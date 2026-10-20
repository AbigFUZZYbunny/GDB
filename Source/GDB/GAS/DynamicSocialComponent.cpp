// DynamicSocialComponent.cpp
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicLifeComponent.h"
#include "Net/UnrealNetwork.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

UDynamicSocialComponent::UDynamicSocialComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);

    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
}

void UDynamicSocialComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;

    if (GetOwner() && GetOwner()->HasAuthority() && !CharacterPersistentGuid.IsValid())
    {
        CharacterPersistentGuid = FGuid::NewGuid();
    }
    PersonalityProfile.FoodAndDrinkPreferences.SyncMapFromChangesArray();
    PersonalityProfile.ActivityPreferences.SyncMapFromChangesArray();
}

void UDynamicSocialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UDynamicSocialComponent, CharacterPersistentGuid);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedProfessions);
    DOREPLIFETIME(UDynamicSocialComponent, PersonalityProfile);
    DOREPLIFETIME_CONDITION(UDynamicSocialComponent, ReplicatedRelationships, COND_InitialOrOwner);
    DOREPLIFETIME_CONDITION(UDynamicSocialComponent, ReplicatedKnownPlaces, COND_InitialOrOwner);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedNeedsAndMoods);
}

void FDynamicRelationshipEdgeItem::PreReplicatedRemove(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleRelationshipEdgeReplicated(*this, true);
}
void FDynamicRelationshipEdgeItem::PostReplicatedAdd(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleRelationshipEdgeReplicated(*this, false);
}
void FDynamicRelationshipEdgeItem::PostReplicatedChange(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleRelationshipEdgeReplicated(*this, false);
}

void FDynamicKnownPlaceItem::PreReplicatedRemove(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleKnownPlaceReplicated(*this, true);
}
void FDynamicKnownPlaceItem::PostReplicatedAdd(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleKnownPlaceReplicated(*this, false);
}
void FDynamicKnownPlaceItem::PostReplicatedChange(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleKnownPlaceReplicated(*this, false);
}

void FDynamicNeedStateItem::PreReplicatedRemove(const FFastNeedStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleNeedStateReplicated(*this, true);
}
void FDynamicNeedStateItem::PostReplicatedAdd(const FFastNeedStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleNeedStateReplicated(*this, false);
}
void FDynamicNeedStateItem::PostReplicatedChange(const FFastNeedStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleNeedStateReplicated(*this, false);
}

void UDynamicSocialComponent::OnRep_PersonalityProfile()
{
    PersonalityProfile.FoodAndDrinkPreferences.SyncMapFromChangesArray();
    PersonalityProfile.ActivityPreferences.SyncMapFromChangesArray();
}

void UDynamicSocialComponent::OnRep_RelationshipEdges()
{
    RebuildRelationshipCache();
}

void UDynamicSocialComponent::OnRep_KnownPlaces()
{
    RebuildPlaceCache();
}

void UDynamicSocialComponent::OnRep_NeedStates()
{
    RebuildNeedCache();
}

void UDynamicSocialComponent::HandleRelationshipEdgeReplicated(const FDynamicRelationshipEdgeItem& Item, bool bRemoved)
{
    RebuildRelationshipCache();
    float DecayedAffinity = Item.BaseAffinity;
    float DummyT, DummyF, DummyD;
    if (!bRemoved)
    {
        GetDecayedRelationship(Item.TargetEntityGuid, DecayedAffinity, DummyT, DummyF, DummyD);
    }
    OnRelationshipChanged.Broadcast(Item.TargetEntityGuid, Item.PrimaryRelationshipTag, bRemoved ? 0.0f : DecayedAffinity);
}

void UDynamicSocialComponent::HandleKnownPlaceReplicated(const FDynamicKnownPlaceItem& Item, bool bRemoved)
{
    RebuildPlaceCache();
}

void UDynamicSocialComponent::HandleNeedStateReplicated(const FDynamicNeedStateItem& Item, bool bRemoved)
{
    RebuildNeedCache();
    OnNeedOrMoodChanged.Broadcast(Item.NeedOrMoodTag, bRemoved ? 0.0f : GetCurrentNeedOrMoodValue(Item.NeedOrMoodTag));
}

void UDynamicSocialComponent::RebuildRelationshipCache()
{
    GuidToEdgeIndexMap.Empty(ReplicatedRelationships.Items.Num());
    for (int32 i = 0; i < ReplicatedRelationships.Items.Num(); ++i)
    {
        if (ReplicatedRelationships.Items[i].TargetEntityGuid.IsValid())
        {
            GuidToEdgeIndexMap.Add(ReplicatedRelationships.Items[i].TargetEntityGuid, i);
        }
    }
}

void UDynamicSocialComponent::RebuildPlaceCache()
{
    RoleToPlaceIndexMap.Empty(ReplicatedKnownPlaces.Items.Num());
    for (int32 i = 0; i < ReplicatedKnownPlaces.Items.Num(); ++i)
    {
        if (ReplicatedKnownPlaces.Items[i].PlaceRoleTag.IsValid())
        {
            RoleToPlaceIndexMap.Add(ReplicatedKnownPlaces.Items[i].PlaceRoleTag, i);
        }
    }
}

void UDynamicSocialComponent::RebuildNeedCache()
{
    TagToNeedIndexMap.Empty(ReplicatedNeedsAndMoods.Items.Num());
    for (int32 i = 0; i < ReplicatedNeedsAndMoods.Items.Num(); ++i)
    {
        if (ReplicatedNeedsAndMoods.Items[i].NeedOrMoodTag.IsValid())
        {
            TagToNeedIndexMap.Add(ReplicatedNeedsAndMoods.Items[i].NeedOrMoodTag, i);
        }
    }
}

float UDynamicSocialComponent::ApplyContextualDecay(float BaseVal, float DecayRatePerDay, float MinFloor, double ElapsedSeconds, ERelationshipDecayCurve Curve) const
{
    if (Curve == ERelationshipDecayCurve::None || DecayRatePerDay <= 0.0f || ElapsedSeconds <= 0.0)
    {
        return BaseVal;
    }

    const float ElapsedDays = static_cast<float>(ElapsedSeconds / SecondsPerGameDay);
    const float Sign = (BaseVal >= 0.0f) ? 1.0f : -1.0f;
    const float AbsVal = FMath::Abs(BaseVal);
    if (AbsVal <= MinFloor) return BaseVal;

    float DecayedMag = AbsVal;
    switch (Curve)
    {
        case ERelationshipDecayCurve::Linear:
            DecayedMag = FMath::Max(MinFloor, AbsVal - (DecayRatePerDay * ElapsedDays));
            break;
        case ERelationshipDecayCurve::Exponential:
            DecayedMag = MinFloor + (AbsVal - MinFloor) * FMath::Exp(-DecayRatePerDay * ElapsedDays);
            break;
        case ERelationshipDecayCurve::StepThreshold:
        {
            const int32 WholeDays = FMath::FloorToInt(ElapsedDays);
            DecayedMag = FMath::Max(MinFloor, AbsVal - (DecayRatePerDay * static_cast<float>(WholeDays)));
            break;
        }
        default:
            break;
    }
    return DecayedMag * Sign;
}

bool UDynamicSocialComponent::GetDecayedRelationship(const FGuid& TargetGuid, float& OutAffinity, float& OutTrust, float& OutFamiliarity, float& OutIndebtedness) const
{
    const int32* FoundIdx = GuidToEdgeIndexMap.Find(TargetGuid);
    if (!FoundIdx || !ReplicatedRelationships.Items.IsValidIndex(*FoundIdx)) return false;

    const FDynamicRelationshipEdgeItem& Edge = ReplicatedRelationships.Items[*FoundIdx];
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : Edge.LastInteractionWorldTime;
    const double Elapsed = FMath::Max(0.0, Now - Edge.LastInteractionWorldTime);

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicRelationshipTypeDef* TypeDef = GS ? GS->RelationshipTypeRegistry.Find(Edge.PrimaryRelationshipTag) : nullptr;

    if (!TypeDef)
    {
        OutAffinity = Edge.BaseAffinity;
        OutTrust = Edge.BaseTrust;
        OutFamiliarity = Edge.BaseFamiliarity;
        OutIndebtedness = Edge.BaseIndebtedness;
        return true;
    }

    OutAffinity = ApplyContextualDecay(Edge.BaseAffinity, TypeDef->AffinityDecayRatePerDay, TypeDef->MinAffinityDecayFloor, Elapsed, TypeDef->DecayCurve);
    OutTrust = ApplyContextualDecay(Edge.BaseTrust, TypeDef->AffinityDecayRatePerDay * 0.5f, TypeDef->MinAffinityDecayFloor, Elapsed, TypeDef->DecayCurve);
    OutFamiliarity = ApplyContextualDecay(Edge.BaseFamiliarity, TypeDef->FamiliarityDecayRatePerDay, TypeDef->MinFamiliarityDecayFloor, Elapsed, TypeDef->DecayCurve);
    OutIndebtedness = ApplyContextualDecay(Edge.BaseIndebtedness, TypeDef->DebtDecayRatePerDay, 0.0f, Elapsed, TypeDef->DecayCurve);
    return true;
}

void UDynamicSocialComponent::ServerRecordSocialInteraction(
    const FGuid& TargetGuid,
    AActor* OptionalTargetActor,
    ERelationshipTargetKind TargetKind,
    FGameplayTag RelationshipTag,
    float AffinityDelta,
    float TrustDelta,
    float FamiliarityDelta,
    float DebtDelta,
    const FGameplayTagContainer& AddedContextTags)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetGuid.IsValid()) return;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicRelationshipTypeDef* TypeDef = (GS && RelationshipTag.IsValid()) ? GS->RelationshipTypeRegistry.Find(RelationshipTag) : nullptr;
    const float Volatility = TypeDef ? TypeDef->VolatilityScalar : 1.0f;

    if (const int32* ExistingIdx = GuidToEdgeIndexMap.Find(TargetGuid))
    {
        FDynamicRelationshipEdgeItem& Edge = ReplicatedRelationships.Items[*ExistingIdx];

        // Bake current decayed values before applying new deltas
        float CurA, CurT, CurF, CurD;
        GetDecayedRelationship(TargetGuid, CurA, CurT, CurF, CurD);

        if (RelationshipTag.IsValid()) Edge.PrimaryRelationshipTag = RelationshipTag;
        Edge.SecondaryContextTags.AppendTags(AddedContextTags);
        Edge.CachedTargetActor = OptionalTargetActor;
        Edge.BaseAffinity = FMath::Clamp(CurA + (AffinityDelta * Volatility), -1.0f, 1.0f);
        Edge.BaseTrust = FMath::Clamp(CurT + (TrustDelta * Volatility), -1.0f, 1.0f);
        Edge.BaseFamiliarity = FMath::Clamp(CurF + FamiliarityDelta, 0.0f, 1.0f);
        Edge.BaseIndebtedness = FMath::Clamp(CurD + DebtDelta, -100.0f, 100.0f);
        Edge.LastInteractionWorldTime = Now;

        ReplicatedRelationships.MarkItemDirty(Edge);
        OnRelationshipChanged.Broadcast(TargetGuid, Edge.PrimaryRelationshipTag, Edge.BaseAffinity);
        return;
    }

    FDynamicRelationshipEdgeItem NewEdge;
    NewEdge.TargetEntityGuid = TargetGuid;
    NewEdge.CachedTargetActor = OptionalTargetActor;
    NewEdge.TargetKind = TargetKind;
    NewEdge.PrimaryRelationshipTag = RelationshipTag;
    NewEdge.SecondaryContextTags = AddedContextTags;
    NewEdge.BaseAffinity = FMath::Clamp((TypeDef ? TypeDef->DefaultAffinity : 0.0f) + (AffinityDelta * Volatility), -1.0f, 1.0f);
    NewEdge.BaseTrust = FMath::Clamp((TypeDef ? TypeDef->DefaultTrust : 0.0f) + (TrustDelta * Volatility), -1.0f, 1.0f);
    NewEdge.BaseFamiliarity = FMath::Clamp((TypeDef ? TypeDef->DefaultFamiliarity : 0.1f) + FamiliarityDelta, 0.0f, 1.0f);
    NewEdge.BaseIndebtedness = DebtDelta;
    NewEdge.LastInteractionWorldTime = Now;

    FDynamicRelationshipEdgeItem& AddedRef = ReplicatedRelationships.Items.Add_GetRef(NewEdge);
    ReplicatedRelationships.MarkItemDirty(AddedRef);
    RebuildRelationshipCache();
    OnRelationshipChanged.Broadcast(TargetGuid, AddedRef.PrimaryRelationshipTag, AddedRef.BaseAffinity);
}

void UDynamicSocialComponent::ServerRegisterKnownPlace(FGameplayTag PlaceRoleTag, FGameplayTag LandmarkOrArchetypeTag, const FGuid& ActorGuid, const FVector& WorldLocation, float Confidence)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !PlaceRoleTag.IsValid()) return;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (const int32* ExistingIdx = RoleToPlaceIndexMap.Find(PlaceRoleTag))
    {
        FDynamicKnownPlaceItem& Place = ReplicatedKnownPlaces.Items[*ExistingIdx];
        Place.PlaceArchetypeOrLandmarkTag = LandmarkOrArchetypeTag;
        Place.LinkedActorGuid = ActorGuid;
        Place.LastKnownLocation = WorldLocation;
        Place.ConfidenceScore = FMath::Clamp(Confidence, 0.0f, 1.0f);
        Place.LastVisitedWorldTime = Now;
        ReplicatedKnownPlaces.MarkItemDirty(Place);
        return;
    }

    FDynamicKnownPlaceItem NewPlace;
    NewPlace.PlaceRoleTag = PlaceRoleTag;
    NewPlace.PlaceArchetypeOrLandmarkTag = LandmarkOrArchetypeTag;
    NewPlace.LinkedActorGuid = ActorGuid;
    NewPlace.LastKnownLocation = WorldLocation;
    NewPlace.ConfidenceScore = FMath::Clamp(Confidence, 0.0f, 1.0f);
    NewPlace.LastVisitedWorldTime = Now;

    FDynamicKnownPlaceItem& Added = ReplicatedKnownPlaces.Items.Add_GetRef(NewPlace);
    ReplicatedKnownPlaces.MarkItemDirty(Added);
    RebuildPlaceCache();
}

bool UDynamicSocialComponent::GetKnownPlaceByRole(FGameplayTag PlaceRoleTag, FDynamicKnownPlaceItem& OutPlace) const
{
    if (const int32* FoundIdx = RoleToPlaceIndexMap.Find(PlaceRoleTag))
    {
        if (ReplicatedKnownPlaces.Items.IsValidIndex(*FoundIdx))
        {
            OutPlace = ReplicatedKnownPlaces.Items[*FoundIdx];
            return true;
        }
    }
    return false;
}

float UDynamicSocialComponent::GetCurrentNeedOrMoodValue(FGameplayTag NeedOrMoodTag) const
{
    const int32* FoundIdx = TagToNeedIndexMap.Find(NeedOrMoodTag);
    if (!FoundIdx || !ReplicatedNeedsAndMoods.Items.IsValidIndex(*FoundIdx)) return 100.0f;

    const FDynamicNeedStateItem& Item = ReplicatedNeedsAndMoods.Items[*FoundIdx];
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : Item.LastEvaluatedWorldTime;
    const float ElapsedHours = static_cast<float>(FMath::Max(0.0, Now - Item.LastEvaluatedWorldTime) / 3600.0);
    return FMath::Clamp(Item.CurrentValue + (Item.ShiftRatePerHour * ElapsedHours), 0.0f, 100.0f);
}

void UDynamicSocialComponent::ServerModifyNeedOrMoodBP(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag)
{
    ServerModifyNeedOrMood(NeedOrMoodTag, BaseDelta, ConsumedItemOrActivityTag);
}

void UDynamicSocialComponent::ServerModifyNeedOrMood(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NeedOrMoodTag.IsValid()) return;

    float FinalDelta = BaseDelta;
    if (ConsumedItemOrActivityTag.IsValid())
    {
        if (const float* PrefMult = PersonalityProfile.FoodAndDrinkPreferences.LocalLookupMap.Find(ConsumedItemOrActivityTag))
        {
            FinalDelta *= *PrefMult;
        }
        else if (const float* ActMult = PersonalityProfile.ActivityPreferences.LocalLookupMap.Find(ConsumedItemOrActivityTag))
        {
            FinalDelta *= *ActMult;
        }
    }

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (const int32* FoundIdx = TagToNeedIndexMap.Find(NeedOrMoodTag))
    {
        FDynamicNeedStateItem& Item = ReplicatedNeedsAndMoods.Items[*FoundIdx];
        const float CurDecayed = GetCurrentNeedOrMoodValue(NeedOrMoodTag);
        Item.CurrentValue = FMath::Clamp(CurDecayed + FinalDelta, 0.0f, 100.0f);
        Item.LastEvaluatedWorldTime = Now;
        ReplicatedNeedsAndMoods.MarkItemDirty(Item);
        OnNeedOrMoodChanged.Broadcast(NeedOrMoodTag, Item.CurrentValue);
        return;
    }

    FDynamicNeedStateItem NewItem;
    NewItem.NeedOrMoodTag = NeedOrMoodTag;
    NewItem.CurrentValue = FMath::Clamp(100.0f + FinalDelta, 0.0f, 100.0f);
    NewItem.LastEvaluatedWorldTime = Now;

    FDynamicNeedStateItem& Added = ReplicatedNeedsAndMoods.Items.Add_GetRef(NewItem);
    ReplicatedNeedsAndMoods.MarkItemDirty(Added);
    RebuildNeedCache();
    OnNeedOrMoodChanged.Broadcast(NeedOrMoodTag, Added.CurrentValue);
}

void UDynamicSocialComponent::ServerAddOrUpdateProfession(FGameplayTag ProfessionTag, float InitialProficiency, float Priority)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ProfessionTag.IsValid()) return;

    for (FDynamicAssignedProfessionItem& Prof : ReplicatedProfessions.Items)
    {
        if (Prof.ProfessionTag == ProfessionTag)
        {
            Prof.ProficiencyLevel = InitialProficiency;
            Prof.ProfessionPriorityWeight = Priority;
            ReplicatedProfessions.MarkItemDirty(Prof);
            return;
        }
    }

    FDynamicAssignedProfessionItem NewProf;
    NewProf.ProfessionTag = ProfessionTag;
    NewProf.ProficiencyLevel = InitialProficiency;
    NewProf.ProfessionPriorityWeight = Priority;

    FDynamicAssignedProfessionItem& Added = ReplicatedProfessions.Items.Add_GetRef(NewProf);
    ReplicatedProfessions.MarkItemDirty(Added);
}

FGameplayTag UDynamicSocialComponent::EvaluateCurrentExpectedActivity(float CurrentGameHour24, FGameplayTag& OutTargetPlaceRoleTag) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || ReplicatedProfessions.Items.Num() == 0) return FGameplayTag::EmptyTag;

    const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    const UDynamicAttributeSet* AttrSet = CharOwner ? CharOwner->GetDynamicAttributeSet() : nullptr;

    // 1. Evaluate emergency stat/need priorities across all professions first
    for (const FDynamicAssignedProfessionItem& ProfItem : ReplicatedProfessions.Items)
    {
        const FDynamicProfessionDef* ProfDef = GS->ProfessionMasterRegistry.Find(ProfItem.ProfessionTag);
        if (!ProfDef) continue;

        for (const FDynamicStatPriorityRule& StatRule : ProfDef->StatPriorities)
        {
            if (TagToNeedIndexMap.Contains(StatRule.AttributeOrNeedTag))
            {
                const float NeedRatio = GetCurrentNeedOrMoodValue(StatRule.AttributeOrNeedTag) / 100.0f;
                if (NeedRatio <= StatRule.CriticalThresholdRatio)
                {
                    OutTargetPlaceRoleTag = FGameplayTag::RequestGameplayTag(FName("Place.FoodSource"), false);
                    return StatRule.AttributeOrNeedTag;
                }
            }
            else if (AttrSet && StatRule.AttributeOrNeedTag.IsValid())
            {
                const float CurVal = AttrSet->GetDynamicAttributeValue(StatRule.AttributeOrNeedTag, 0);
                if (CurVal <= StatRule.CriticalThresholdRatio * 100.0f)
                {
                    OutTargetPlaceRoleTag = FGameplayTag::RequestGameplayTag(FName("Place.Home"), false);
                    return StatRule.AttributeOrNeedTag;
                }
            }
        }
    }

    // 2. Scan all active professions for schedule blocks matching the current time window, weighted by profession priority
    FGameplayTag BestActivity = FGameplayTag::EmptyTag;
    float HighestAdherenceWeight = -1.0f;

    for (const FDynamicAssignedProfessionItem& ProfItem : ReplicatedProfessions.Items)
    {
        const FDynamicProfessionDef* ProfDef = GS->ProfessionMasterRegistry.Find(ProfItem.ProfessionTag);
        if (!ProfDef) continue;

        for (const FDynamicScheduleBlockDef& Block : ProfDef->DefaultDailySchedule)
        {
            const bool bInWindow = (Block.StartHour24 <= Block.EndHour24)
                ? (CurrentGameHour24 >= Block.StartHour24 && CurrentGameHour24 < Block.EndHour24)
                : (CurrentGameHour24 >= Block.StartHour24 || CurrentGameHour24 < Block.EndHour24);

            if (bInWindow)
            {
                float CombinedScore = Block.ScheduleAdherenceWeight * ProfItem.ProfessionPriorityWeight;
                if (CombinedScore > HighestAdherenceWeight)
                {
                    HighestAdherenceWeight = CombinedScore;
                    OutTargetPlaceRoleTag = Block.PreferredPlaceRoleTag;
                    BestActivity = Block.ScheduledActivityTag;
                }
            }
        }
    }

    return BestActivity;
}

bool UDynamicSocialComponent::ExportComputeNodePayloadJson(FString& OutTelemetryJson) const
{
    TSharedRef<FJsonObject> RootObj = MakeShareable(new FJsonObject());
    RootObj->SetStringField(TEXT("EntityGuid"), CharacterPersistentGuid.ToString());

    // 1. Serialize Multiple Assigned Professions into a JSON Array
    TArray<TSharedPtr<FJsonValue>> ProfArray;
    for (const FDynamicAssignedProfessionItem& ProfItem : ReplicatedProfessions.Items)
    {
        if (ProfItem.ProfessionTag.IsValid())
        {
            TSharedRef<FJsonObject> ProfObj = MakeShareable(new FJsonObject());
            ProfObj->SetStringField(TEXT("ProfessionTag"), ProfItem.ProfessionTag.ToString());
            ProfObj->SetNumberField(TEXT("Proficiency"), ProfItem.ProficiencyLevel);
            ProfObj->SetNumberField(TEXT("Priority"), ProfItem.ProfessionPriorityWeight);
            ProfArray.Add(MakeShareable(new FJsonValueObject(ProfObj)));
        }
    }
    RootObj->SetArrayField(TEXT("Professions"), ProfArray);

    // 2. Needs & Moods Snapshot
    TSharedPtr<FJsonObject> NeedsObj = MakeShareable(new FJsonObject());
    for (const FDynamicNeedStateItem& Need : ReplicatedNeedsAndMoods.Items)
    {
        if (Need.NeedOrMoodTag.IsValid())
        {
            NeedsObj->SetNumberField(Need.NeedOrMoodTag.ToString(), GetCurrentNeedOrMoodValue(Need.NeedOrMoodTag));
        }
    }
    RootObj->SetObjectField(TEXT("NeedsAndMoods"), NeedsObj);

    // 3. Decayed Relationships Matrix
    TArray<TSharedPtr<FJsonValue>> RelArray;
    for (const FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        float A, T, F, D;
        if (GetDecayedRelationship(Edge.TargetEntityGuid, A, T, F, D))
        {
            TSharedRef<FJsonObject> EdgeObj = MakeShareable(new FJsonObject());
            EdgeObj->SetStringField(TEXT("TargetGuid"), Edge.TargetEntityGuid.ToString());
            EdgeObj->SetStringField(TEXT("Tag"), Edge.PrimaryRelationshipTag.ToString());
            EdgeObj->SetNumberField(TEXT("Affinity"), A);
            EdgeObj->SetNumberField(TEXT("Trust"), T);
            EdgeObj->SetNumberField(TEXT("Familiarity"), F);
            EdgeObj->SetNumberField(TEXT("Debt"), D);
            RelArray.Add(MakeShareable(new FJsonValueObject(EdgeObj)));
        }
    }
    RootObj->SetArrayField(TEXT("Relationships"), RelArray);

    // 4. Spatial Places Memory
    TArray<TSharedPtr<FJsonValue>> PlacesArray;
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        TSharedRef<FJsonObject> PlaceObj = MakeShareable(new FJsonObject());
        PlaceObj->SetStringField(TEXT("Role"), Place.PlaceRoleTag.ToString());
        PlaceObj->SetStringField(TEXT("Landmark"), Place.PlaceArchetypeOrLandmarkTag.ToString());
        PlaceObj->SetNumberField(TEXT("Confidence"), Place.ConfidenceScore);
        PlacesArray.Add(MakeShareable(new FJsonValueObject(PlaceObj)));
    }
    RootObj->SetArrayField(TEXT("KnownPlaces"), PlacesArray);

    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutTelemetryJson);
    return FJsonSerializer::Serialize(RootObj, Writer);
}

// --- Fast-Array Item Callback Implementations for Multiple Professions ---
void FDynamicAssignedProfessionItem::PreReplicatedRemove(const FFastAssignedProfessionArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->HandleProfessionReplicated(*this, true);
    }
}

void FDynamicAssignedProfessionItem::PostReplicatedAdd(const FFastAssignedProfessionArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->HandleProfessionReplicated(*this, false);
    }
}

void FDynamicAssignedProfessionItem::PostReplicatedChange(const FFastAssignedProfessionArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->HandleProfessionReplicated(*this, false);
    }
}

void UDynamicSocialComponent::HandleProfessionReplicated(const FDynamicAssignedProfessionItem& Item, bool bRemoved)
{
    // Client-side assigned profession cache sync
}

void UDynamicSocialComponent::OnRep_Professions()
{
    // Client-side assigned professions update hook
}

bool UDynamicSocialComponent::ServerRemoveProfession(FGameplayTag ProfessionTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ProfessionTag.IsValid()) return false;

    int32 RemovedCount = ReplicatedProfessions.Items.RemoveAll([ProfessionTag](const FDynamicAssignedProfessionItem& Item) {
        return Item.ProfessionTag == ProfessionTag;
    });

    if (RemovedCount > 0)
    {
        ReplicatedProfessions.MarkArrayDirty();
        return true;
    }
    return false;
}

bool UDynamicSocialComponent::CanInitiateSocialInteraction(FGameplayTag InteractionType, ADynamicCharacter* TargetCharacter) const
{
    if (!TargetCharacter) return false;

    // Check if interaction requires adulthood
    if (InteractionType.MatchesTag(FGameplayTag::RequestGameplayTag(TEXT("Interaction.Social.Romantic"))))
    {
        UDynamicLifeComponent* SourceLife = GetOwner()->FindComponentByClass<UDynamicLifeComponent>();
        UDynamicLifeComponent* TargetLife = TargetCharacter->FindComponentByClass<UDynamicLifeComponent>();

        if (SourceLife && TargetLife)
        {
            // Both parties must be legally/biologically categorized as adults according to their race's aging profile
            if (!SourceLife->IsCharacterAdult() || !TargetLife->IsCharacterAdult())
            {
                return false; // Blocks interaction entirely if either is a child/infant
            }
        }
    }
    return true;
}
