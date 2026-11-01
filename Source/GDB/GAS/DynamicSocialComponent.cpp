// DynamicSocialComponent.cpp
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicBuildingSubsystem.h"
#include "AbilitySystemComponent.h"
#include "DynamicQuestComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "TimerManager.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Net/UnrealNetwork.h"

UDynamicSocialComponent::UDynamicSocialComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);

    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedProfessions.OwningSocialComp = this;
    ReplicatedCrimeMemories.OwningSocialComp = this;
}

void UDynamicSocialComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedProfessions.OwningSocialComp = this;
    ReplicatedCrimeMemories.OwningSocialComp = this;

    if (AActor* OwnerActor = GetOwner())
    {
        if (OwnerActor->HasAuthority() && !CharacterPersistentGuid.IsValid())
        {
            CharacterPersistentGuid = FGuid::NewGuid();
        }

        if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(OwnerActor))
        {
            if (UWorld* World = GetWorld())
            {
                if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
                {
                    GS->RegisterCharacterByGuid(CharacterPersistentGuid, DynChar);
                }
            }
        }
    }

    RebuildRelationshipCache();
    RebuildNeedCache();
    RebuildPlaceCache();
}

void UDynamicSocialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_CalloutTimeout);

        if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
        {
            GS->UnregisterCharacterByGuid(CharacterPersistentGuid, Cast<ADynamicCharacter>(GetOwner()));
        }
    }
    Super::EndPlay(EndPlayReason);
}

void UDynamicSocialComponent::OnRep_CharacterPersistentGuid(const FGuid& OldGuid)
{
    if (ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (UWorld* World = GetWorld())
        {
            if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
            {
                if (OldGuid.IsValid())
                {
                    GS->UnregisterCharacterByGuid(OldGuid, DynChar);
                }
                if (CharacterPersistentGuid.IsValid())
                {
                    GS->RegisterCharacterByGuid(CharacterPersistentGuid, DynChar);
                }
            }
        }
    }
}

void UDynamicSocialComponent::SetCharacterPersistentGuid(const FGuid& NewGuid)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NewGuid.IsValid() || CharacterPersistentGuid == NewGuid)
    {
        return;
    }

    const FGuid OldGuid = CharacterPersistentGuid;
    CharacterPersistentGuid = NewGuid;
    OnRep_CharacterPersistentGuid(OldGuid);
}

void UDynamicSocialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicSocialComponent, CharacterPersistentGuid);
    DOREPLIFETIME(UDynamicSocialComponent, CharacterDisplayName);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedRelationships);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedNeedsAndMoods);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedKnownPlaces);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedProfessions);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedCrimeMemories);
    DOREPLIFETIME(UDynamicSocialComponent, bCloudAISuspended);
}

void UDynamicSocialComponent::OnRep_Relationships() { RebuildRelationshipCache(); }
void UDynamicSocialComponent::OnRep_NeedsAndMoods() { RebuildNeedCache(); }
void UDynamicSocialComponent::OnRep_KnownPlaces() { RebuildPlaceCache(); }
void UDynamicSocialComponent::OnRep_Professions() {}
void UDynamicSocialComponent::OnRep_CrimeMemories() {}
void UDynamicSocialComponent::HandleSingleCrimeMemoryReplicated(const FCrimeMemoryItem& Item) {}

void UDynamicSocialComponent::RebuildRelationshipCache()
{
    CachedRelationshipIndices.Empty(ReplicatedRelationships.Items.Num());
    const bool bIsClient = (!GetOwner() || !GetOwner()->HasAuthority());
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;

    for (int32 i = 0; i < ReplicatedRelationships.Items.Num(); ++i)
    {
        FDynamicRelationshipEdgeItem& Edge = ReplicatedRelationships.Items[i];
        if (Edge.TargetEntityGuid.IsValid())
        {
            CachedRelationshipIndices.Add(Edge.TargetEntityGuid, i);
        }

        // Dequantize 1-byte PackedValue into ExactValue on client proxies using JSON rules
        if (bIsClient)
        {
            const FDynamicRelationshipTypeDef* RelDef = GS ? GS->RelationshipTypeRegistry.Find(Edge.PrimaryRelationshipTag) : nullptr;
            for (FDynamicRelationshipMetricEntry& Metric : Edge.Metrics)
            {
                const FRelationshipMetricRule* Rule = RelDef ? RelDef->FindMetricRule(Metric.MetricTag) : nullptr;
                const float MinVal = Rule ? Rule->MinValue : -100.0f;
                const float MaxVal = Rule ? Rule->MaxValue : 100.0f;
                Metric.ExactValue = DynamicQuantization::DequantizeRange(Metric.PackedValue, MinVal, MaxVal);
            }
        }
    }
}

void UDynamicSocialComponent::RebuildNeedCache()
{
    CachedNeeds.Empty(ReplicatedNeedsAndMoods.Items.Num());
    const bool bIsClient = (!GetOwner() || !GetOwner()->HasAuthority());

    for (FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        if (bIsClient)
        {
            Item.LocalExactValue = DynamicQuantization::DequantizeScale100(Item.PackedCurrentValue);
        }
        CachedNeeds.Add(Item.NeedTag, Item.LocalExactValue);
    }
}

void UDynamicSocialComponent::RebuildPlaceCache()
{
    CachedPlacesByRole.Empty(ReplicatedKnownPlaces.Items.Num());
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        CachedPlacesByRole.Add(Place.PlaceRoleTag, Place);
    }
}

bool UDynamicSocialComponent::GetNeedValue(const FGameplayTag& NeedTag, float& OutValue) const
{
    if (const float* Found = CachedNeeds.Find(NeedTag))
    {
        OutValue = *Found;
        return true;
    }
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        if (Item.NeedTag == NeedTag)
        {
            OutValue = Item.LocalExactValue;
            return true;
        }
    }
    OutValue = 0.0f;
    return false;
}

float UDynamicSocialComponent::GetDecayedRelationshipMetric(const FGuid& TargetEntityGuid, const FGameplayTag& MetricTag) const
{
    const FDynamicRelationshipEdgeItem* Edge = FindRelationshipByGuid(TargetEntityGuid);
    if (!Edge) return 0.0f;

    const float RawVal = Edge->GetMetricValue(MetricTag);
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!World || !GS || Edge->LastInteractionWorldTime <= 0.0) return RawVal;

    const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(Edge->PrimaryRelationshipTag);
    const FRelationshipMetricRule* Rule = RelDef ? RelDef->FindMetricRule(MetricTag) : nullptr;
    if (!Rule || Rule->DecayCurve == ERelationshipDecayCurve::None || Rule->DecayRatePerDay <= 0.0f)
    {
        return RawVal;
    }

    const double ElapsedRealSecs = FMath::Max(0.0, World->GetTimeSeconds() - Edge->LastInteractionWorldTime);
    const float SecsPerGameDay = FMath::Max(1.0f, (GS->CalendarConfig.HoursPerDay * 3600.0f) / FMath::Max(0.01f, GS->CalendarConfig.TimeScalar));
    const float ElapsedDays = static_cast<float>(ElapsedRealSecs) / SecsPerGameDay;

    switch (Rule->DecayCurve)
    {
        case ERelationshipDecayCurve::Linear:
            return FMath::FInterpConstantTo(RawVal, Rule->DecayTargetFloor, ElapsedDays, Rule->DecayRatePerDay);
        case ERelationshipDecayCurve::Exponential:
            return Rule->DecayTargetFloor + (RawVal - Rule->DecayTargetFloor) * FMath::Exp(-Rule->DecayRatePerDay * ElapsedDays);
        case ERelationshipDecayCurve::StepThreshold:
            return (ElapsedDays >= (1.0f / FMath::Max(0.001f, Rule->DecayRatePerDay))) ? Rule->DecayTargetFloor : RawVal;
        default:
            return RawVal;
    }
}

bool UDynamicSocialComponent::GetKnownPlaceByRole(const FGameplayTag& RoleTag, FDynamicKnownPlaceItem& OutPlace) const
{
    if (const FDynamicKnownPlaceItem* Found = CachedPlacesByRole.Find(RoleTag))
    {
        OutPlace = *Found;
        return true;
    }
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        if (Place.PlaceRoleTag == RoleTag)
        {
            OutPlace = Place;
            return true;
        }
    }
    return false;
}

const FDynamicRelationshipEdgeItem* UDynamicSocialComponent::FindRelationshipWithActor(const AActor* TargetActor) const
{
    if (!TargetActor) return nullptr;
    const UDynamicSocialComponent* TargetSocial = TargetActor->FindComponentByClass<UDynamicSocialComponent>();
    if (!TargetSocial) return nullptr;
    return FindRelationshipByGuid(TargetSocial->CharacterPersistentGuid);
}

const FDynamicRelationshipEdgeItem* UDynamicSocialComponent::FindRelationshipByGuid(const FGuid& TargetGuid) const
{
    if (!TargetGuid.IsValid()) return nullptr;
    if (const int32* CachedIdx = CachedRelationshipIndices.Find(TargetGuid))
    {
        if (ReplicatedRelationships.Items.IsValidIndex(*CachedIdx) &&
            ReplicatedRelationships.Items[*CachedIdx].TargetEntityGuid == TargetGuid)
        {
            return &ReplicatedRelationships.Items[*CachedIdx];
        }
    }
    for (const FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        if (Edge.TargetEntityGuid == TargetGuid)
        {
            return &Edge;
        }
    }
    return nullptr;
}

void UDynamicSocialComponent::ServerRegisterKnownPlace(const FGameplayTag& PlaceTag, const FGameplayTag& RoleTag, const FGuid& AreaGuid, const FVector& WorldLocation, float InitialConfidence)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    for (FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        if (Place.PlaceRoleTag == RoleTag || (AreaGuid.IsValid() && Place.LinkedActorGuid == AreaGuid))
        {
            Place.LastKnownLocation = WorldLocation;
            Place.ConfidenceScore = FMath::Clamp(InitialConfidence, 0.0f, 1.0f);
            Place.LastVisitedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            ReplicatedKnownPlaces.MarkItemDirty(Place);
            RebuildPlaceCache();
            return;
        }
    }

    FDynamicKnownPlaceItem NewPlace;
    NewPlace.PlaceRoleTag = RoleTag;
    NewPlace.PlaceArchetypeOrLandmarkTag = PlaceTag;
    NewPlace.LinkedActorGuid = AreaGuid;
    NewPlace.LastKnownLocation = WorldLocation;
    NewPlace.ConfidenceScore = FMath::Clamp(InitialConfidence, 0.0f, 1.0f);
    NewPlace.LastVisitedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    
    FDynamicKnownPlaceItem& Added = ReplicatedKnownPlaces.Items.Add_GetRef(NewPlace);
    ReplicatedKnownPlaces.MarkItemDirty(Added);
    RebuildPlaceCache();
}

void UDynamicSocialComponent::ServerSetNeedValue(const FGameplayTag& NeedTag, float NewValue)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NeedTag.IsValid()) return;

    const float ClampedVal = FMath::Clamp(NewValue, 0.0f, 100.0f);
    const uint8 Packed = DynamicQuantization::QuantizeScale100(ClampedVal);

    for (FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        if (Item.NeedTag == NeedTag)
        {
            Item.LocalExactValue = ClampedVal;
            Item.PackedCurrentValue = Packed;
            Item.LastEvaluatedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            ReplicatedNeedsAndMoods.MarkItemDirty(Item);
            RebuildNeedCache();
            return;
        }
    }

    FDynamicNeedStateItem NewItem;
    NewItem.NeedTag = NeedTag;
    NewItem.LocalExactValue = ClampedVal;
    NewItem.PackedCurrentValue = Packed;
    NewItem.LastEvaluatedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    
    FDynamicNeedStateItem& Added = ReplicatedNeedsAndMoods.Items.Add_GetRef(NewItem);
    ReplicatedNeedsAndMoods.MarkItemDirty(Added);
    RebuildNeedCache();
}

void UDynamicSocialComponent::ServerRecordSocialInteraction(
    const FGuid& TargetEntityGuid,
    AActor* TargetActor,
    ERelationshipTargetKind TargetKind,
    FGameplayTag RelationshipTypeTag,
    const TArray<FTagFloatPair>& MetricDeltas,
    const FGameplayTagContainer& ContextTags)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetEntityGuid.IsValid()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    FDynamicRelationshipEdgeItem* FoundEdge = nullptr;
    for (FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        if (Edge.TargetEntityGuid == TargetEntityGuid)
        {
            FoundEdge = &Edge;
            break;
        }
    }

    const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    if (!FoundEdge)
    {
        FDynamicRelationshipEdgeItem NewEdge;
        NewEdge.TargetEntityGuid = TargetEntityGuid;
        NewEdge.CachedTargetActor = TargetActor;
        NewEdge.TargetKind = TargetKind;
        NewEdge.PrimaryRelationshipTag = RelationshipTypeTag.IsValid() ? RelationshipTypeTag : FGameplayTag::RequestGameplayTag(TEXT("Relation.Social.Acquaintance"), false);
        NewEdge.SecondaryContextTags = ContextTags;
        NewEdge.LastInteractionWorldTime = CurrentTime;

        if (GS)
        {
            if (const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(NewEdge.PrimaryRelationshipTag))
            {
                for (const FRelationshipMetricRule& Rule : RelDef->SupportedMetrics)
                {
                    const uint8 PackedDef = DynamicQuantization::QuantizeRange(Rule.DefaultValue, Rule.MinValue, Rule.MaxValue);
                    NewEdge.SetMetricValue(Rule.MetricTag, Rule.DefaultValue, PackedDef);
                }
            }
        }
        
        FDynamicRelationshipEdgeItem& Added = ReplicatedRelationships.Items.Add_GetRef(NewEdge);
        ReplicatedRelationships.MarkItemDirty(Added);
        FoundEdge = &Added;
    }

    const FDynamicRelationshipTypeDef* RelDef = GS ? GS->RelationshipTypeRegistry.Find(FoundEdge->PrimaryRelationshipTag) : nullptr;

    for (const FTagFloatPair& Delta : MetricDeltas)
    {
        if (!Delta.KeyTag.IsValid()) continue;

        const FRelationshipMetricRule* Rule = RelDef ? RelDef->FindMetricRule(Delta.KeyTag) : nullptr;
        const float MinVal = Rule ? Rule->MinValue : -100.0f;
        const float MaxVal = Rule ? Rule->MaxValue : 100.0f;
        const float Volatility = Rule ? Rule->VolatilityScalar : 1.0f;
        const bool bClamp = Rule ? Rule->bClampToMinMax : true;

        const float DecayedCurVal = GetDecayedRelationshipMetric(TargetEntityGuid, Delta.KeyTag);
        const float UnclampedNew = DecayedCurVal + (Delta.Value * Volatility);
        const float NewVal = bClamp ? FMath::Clamp(UnclampedNew, MinVal, MaxVal) : UnclampedNew;
        const uint8 PackedVal = DynamicQuantization::QuantizeRange(NewVal, MinVal, MaxVal);

        FoundEdge->SetMetricValue(Delta.KeyTag, NewVal, PackedVal);
    }

    FoundEdge->LastInteractionWorldTime = CurrentTime;
    FoundEdge->CachedTargetActor = TargetActor;
    FoundEdge->SecondaryContextTags.AppendTags(ContextTags);

    ReplicatedRelationships.MarkItemDirty(*FoundEdge);
    RebuildRelationshipCache();
}

void UDynamicSocialComponent::ServerSetCloudAISuspension(bool bSuspended)
{
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        bCloudAISuspended = bSuspended;
    }
}

FString UDynamicSocialComponent::SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const
{
    TSharedRef<FJsonObject> RootObj = MakeShareable(new FJsonObject());
    RootObj->SetStringField(TEXT("AgentGuid"), CharacterPersistentGuid.ToString());
    RootObj->SetStringField(TEXT("RequestType"), UEnum::GetValueAsString(RequestType));

    const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const AActor* OwnerActor = GetOwner();
    const FVector OwnerLoc = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;

    TSharedPtr<FJsonObject> NeedsObj = MakeShareable(new FJsonObject());
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        const bool bIsUrgent = ContextFilterConfig.bFilterUrgentNeedsOnly && (Item.LocalExactValue < 25.0f || Item.LocalExactValue > 75.0f);
        const bool bHasChanged = ContextFilterConfig.bDeltaOnlySerialization && (Item.LastEvaluatedWorldTime >= LastContextSerializationTime);

        if (!ContextFilterConfig.bFilterUrgentNeedsOnly || bIsUrgent || bHasChanged)
        {
            NeedsObj->SetNumberField(TEXT("PackedCurrentValue"), Item.PackedCurrentValue);
        }
    }
    RootObj->SetObjectField(TEXT("Needs"), NeedsObj);

    TArray<TSharedPtr<FJsonValue>> RelationshipsArray;
    int32 PackedRelationships = 0;

    for (const FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        if (PackedRelationships >= ContextFilterConfig.MaxRelationshipsInPrompt) break;

        bool bInclude = true;
        if (Edge.CachedTargetActor.IsValid())
        {
            const float DistSq = FVector::DistSquared(OwnerLoc, Edge.CachedTargetActor->GetActorLocation());
            if (DistSq > FMath::Square(ContextFilterConfig.SpatialEntityPerceptionRadius))
            {
                bInclude = false;
            }
        }

        if (bInclude)
        {
            TSharedPtr<FJsonObject> RelObj = MakeShareable(new FJsonObject());
            RelObj->SetStringField(TEXT("Target"), Edge.TargetEntityGuid.ToString());
            RelObj->SetStringField(TEXT("Type"), Edge.PrimaryRelationshipTag.ToString());

            TSharedPtr<FJsonObject> MetricsObj = MakeShareable(new FJsonObject());
            for (const FDynamicRelationshipMetricEntry& Metric : Edge.Metrics)
            {
                MetricsObj->SetNumberField(Metric.MetricTag.ToString(), GetDecayedRelationshipMetric(Edge.TargetEntityGuid, Metric.MetricTag));
            }
            RelObj->SetObjectField(TEXT("Metrics"), MetricsObj);
            RelationshipsArray.Add(MakeShareable(new FJsonValueObject(RelObj)));
            PackedRelationships++;
        }
    }
    RootObj->SetArrayField(TEXT("NearbyRelationships"), RelationshipsArray);

    TArray<TSharedPtr<FJsonValue>> PlacesArray;
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        if (!ContextFilterConfig.bDeltaOnlySerialization || Place.LastVisitedWorldTime >= LastContextSerializationTime)
        {
            TSharedPtr<FJsonObject> PlaceObj = MakeShareable(new FJsonObject());
            PlaceObj->SetStringField(TEXT("Role"), Place.PlaceRoleTag.ToString());
            PlaceObj->SetStringField(TEXT("Landmark"), Place.PlaceArchetypeOrLandmarkTag.ToString());
            PlacesArray.Add(MakeShareable(new FJsonValueObject(PlaceObj)));
        }
    }
    RootObj->SetArrayField(TEXT("KnownPlaces"), PlacesArray);

    LastContextSerializationTime = CurrentWorldTime;

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObj, Writer);
    return OutputString;
}

FString UDynamicSocialComponent::ResolveActorDisplayName(const AActor* InActor) const
{
    if (!InActor) return TEXT("traveler");
    if (const UDynamicSocialComponent* Social = InActor->FindComponentByClass<UDynamicSocialComponent>())
    {
        if (!Social->CharacterDisplayName.IsEmpty())
        {
            return Social->CharacterDisplayName;
        }
    }
    return InActor->GetName();
}

FString UDynamicSocialComponent::ExtractFirstName(const FString& FullName) const
{
    FString FirstName, LastName;
    if (FullName.Split(TEXT(" "), &FirstName, &LastName))
    {
        return FirstName;
    }
    return FullName;
}

bool UDynamicSocialComponent::SelectBestUtterance(
    const TArray<FDynamicDialogueUtteranceDef>& CandidateUtterances,
    AActor* TargetActor,
    FDynamicDialogueUtteranceDef& OutSelectedUtterance,
    float& OutComputedWeight) const
{
    OutComputedWeight = 0.0f;
    if (CandidateUtterances.Num() == 0) return false;

    AActor* OwnerActor = GetOwner();
    const UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    const UDynamicAttributeSet* AttrSet = ASC ? Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;

    const UAbilitySystemComponent* TargetASC = TargetActor ? TargetActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    const UDynamicAttributeSet* TargetAttrSet = TargetASC ? Cast<UDynamicAttributeSet>(TargetASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;
    const UDynamicSocialComponent* TargetSocial = TargetActor ? TargetActor->FindComponentByClass<UDynamicSocialComponent>() : nullptr;

    const FDynamicRelationshipEdgeItem* RelationEdge = TargetActor ? FindRelationshipWithActor(TargetActor) : nullptr;

    FGameplayTagContainer SpeakerMoodTags;
    for (const FDynamicNeedStateItem& MoodItem : ReplicatedNeedsAndMoods.Items)
    {
        SpeakerMoodTags.AddTag(MoodItem.NeedTag);
    }

    FGameplayTagContainer TargetMoodTags;
    if (TargetSocial)
    {
        for (const FDynamicNeedStateItem& MoodItem : TargetSocial->ReplicatedNeedsAndMoods.Items)
        {
            TargetMoodTags.AddTag(MoodItem.NeedTag);
        }
    }

    struct FEvaluatedCandidate
    {
        const FDynamicDialogueUtteranceDef* Def = nullptr;
        float FinalWeight = 0.0f;
    };

    TArray<FEvaluatedCandidate> EligiblePool;
    float AccumulatedTotalWeight = 0.0f;

    for (const FDynamicDialogueUtteranceDef& Option : CandidateUtterances)
    {
        if (Option.TargetAttributeRequirements.Num() > 0)
        {
            if (!TargetActor) continue;
            bool bTargetAttrsMatch = true;
            for (const FCharacterAttributeRangeDef& Req : Option.TargetAttributeRequirements)
            {
                float Val = 0.0f;
                if (TargetAttrSet) Val = TargetAttrSet->GetDynamicAttributeValue(Req.AttributeTag, 0);
                if (FMath::IsNearlyZero(Val) && TargetSocial) TargetSocial->GetNeedValue(Req.AttributeTag, Val);

                if (Val < Req.MinValue || Val > Req.MaxValue)
                {
                    bTargetAttrsMatch = false;
                    break;
                }
            }
            if (!bTargetAttrsMatch) continue;
        }

        if (Option.TargetAllowedMoodTags.Num() > 0)
        {
            if (!TargetSocial || !TargetMoodTags.HasAny(Option.TargetAllowedMoodTags))
            {
                continue;
            }
        }

        if (Option.RequiredRelationshipTags.Num() > 0)
        {
            if (!RelationEdge) continue;
            const bool bHasPrimary = Option.RequiredRelationshipTags.HasTag(RelationEdge->PrimaryRelationshipTag);
            const bool bHasSecondary = Option.RequiredRelationshipTags.HasAny(RelationEdge->SecondaryContextTags);
            if (!bHasPrimary && !bHasSecondary)
            {
                continue;
            }
        }

        if (Option.AllowedMoodTags.Num() > 0 && !SpeakerMoodTags.HasAny(Option.AllowedMoodTags))
        {
            continue;
        }

        bool bRelationalPassed = true;
        for (const FCharacterAttributeRangeDef& RelReq : Option.RelationalMetricRequirements)
        {
            const float MetricVal = RelationEdge ? GetDecayedRelationshipMetric(RelationEdge->TargetEntityGuid, RelReq.AttributeTag) : 0.0f;
            if (MetricVal < RelReq.MinValue || MetricVal > RelReq.MaxValue)
            {
                bRelationalPassed = false;
                break;
            }
        }
        if (!bRelationalPassed) continue;

        bool bAttributesPassed = true;
        for (const FCharacterAttributeRangeDef& AttrReq : Option.AttributeRequirements)
        {
            float CurrentAttrVal = 0.0f;
            if (AttrSet) CurrentAttrVal = AttrSet->GetDynamicAttributeValue(AttrReq.AttributeTag, 0);
            if (FMath::IsNearlyZero(CurrentAttrVal)) GetNeedValue(AttrReq.AttributeTag, CurrentAttrVal);

            if (CurrentAttrVal < AttrReq.MinValue || CurrentAttrVal > AttrReq.MaxValue)
            {
                bAttributesPassed = false;
                break;
            }
        }
        if (!bAttributesPassed) continue;

        float RunningWeight = FMath::Max(0.01f, Option.SelectionWeight);

        for (const auto& AttrScalar : Option.ProbabilityAttributeScalars.LocalLookupMap)
        {
            float CurrentVal = 0.0f;
            if (AttrSet) CurrentVal = AttrSet->GetDynamicAttributeValue(AttrScalar.Key, 0);
            if (FMath::IsNearlyZero(CurrentVal)) GetNeedValue(AttrScalar.Key, CurrentVal);
            RunningWeight += (CurrentVal * AttrScalar.Value);
        }

        if (TargetAttrSet || TargetSocial)
        {
            for (const auto& TargetAttrScalar : Option.ProbabilityTargetAttributeScalars.LocalLookupMap)
            {
                float TargetVal = 0.0f;
                if (TargetAttrSet) TargetVal = TargetAttrSet->GetDynamicAttributeValue(TargetAttrScalar.Key, 0);
                if (FMath::IsNearlyZero(TargetVal) && TargetSocial) TargetSocial->GetNeedValue(TargetAttrScalar.Key, TargetVal);
                RunningWeight += (TargetVal * TargetAttrScalar.Value);
            }
        }

        if (TargetSocial)
        {
            for (const auto& TargetMoodScalar : Option.ProbabilityTargetMoodScalars.LocalLookupMap)
            {
                float MoodVal = 0.0f;
                if (TargetSocial->GetNeedValue(TargetMoodScalar.Key, MoodVal))
                {
                    RunningWeight += (MoodVal * TargetMoodScalar.Value);
                }
            }
        }

        if (RelationEdge)
        {
            for (const auto& RelScalar : Option.ProbabilityRelationalScalars.LocalLookupMap)
            {
                const float MetricVal = GetDecayedRelationshipMetric(RelationEdge->TargetEntityGuid, RelScalar.Key);
                RunningWeight += (MetricVal * RelScalar.Value);
            }
        }

        for (const auto& MoodScalar : Option.ProbabilityMoodScalars.LocalLookupMap)
        {
            float MoodVal = 0.0f;
            if (GetNeedValue(MoodScalar.Key, MoodVal))
            {
                RunningWeight += (MoodVal * MoodScalar.Value);
            }
        }

        RunningWeight = FMath::Max(0.01f, RunningWeight);
        AccumulatedTotalWeight += RunningWeight;

        FEvaluatedCandidate Candidate;
        Candidate.Def = &Option;
        Candidate.FinalWeight = RunningWeight;
        EligiblePool.Add(Candidate);
    }

    if (EligiblePool.Num() == 0) return false;

    const float RandomRoll = FMath::FRandRange(0.0f, AccumulatedTotalWeight);
    float Tally = 0.0f;

    for (const FEvaluatedCandidate& Pick : EligiblePool)
    {
        Tally += Pick.FinalWeight;
        if (RandomRoll <= Tally)
        {
            OutSelectedUtterance = *Pick.Def;
            OutComputedWeight = Pick.FinalWeight;
            return true;
        }
    }

    OutSelectedUtterance = *EligiblePool.Last().Def;
    OutComputedWeight = EligiblePool.Last().FinalWeight;
    return true;
}

FString UDynamicSocialComponent::FormatDialogueTokens(
    const FString& TemplateString,
    AActor* TargetActor,
    const FString& CustomLocation,
    const FString& CustomTime) const
{
    return FormatDialogueTokens(TemplateString, TargetActor, CustomLocation, CustomTime, TMap<FString, FString>());
}

FString UDynamicSocialComponent::FormatDialogueTokens(
    const FString& TemplateString,
    AActor* TargetActor,
    const FString& CustomLocation,
    const FString& CustomTime,
    const TMap<FString, FString>& CustomWildcards) const
{
    FString Formatted = TemplateString;

    const FString SpeakerName = ResolveActorDisplayName(GetOwner());
    const FString SpeakerFName = ExtractFirstName(SpeakerName);
    const FString TargetName = ResolveActorDisplayName(TargetActor);
    const FString TargetFName = ExtractFirstName(TargetName);

    Formatted = Formatted.Replace(TEXT("%Target%"), *TargetName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%target%"), *TargetName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%Speaker%"), *SpeakerName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%speaker%"), *SpeakerName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%FName%"), *TargetFName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%TargetFName%"), *TargetFName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%SpeakerFName%"), *SpeakerFName, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%location_type%"), *CustomLocation, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%Location%"), *CustomLocation, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%recent_time%"), *CustomTime, ESearchCase::IgnoreCase);
    Formatted = Formatted.Replace(TEXT("%Time%"), *CustomTime, ESearchCase::IgnoreCase);

    for (const auto& WildcardPair : CustomWildcards)
    {
        const FString TokenKey = FString::Printf(TEXT("%%%s%%"), *WildcardPair.Key);
        Formatted = Formatted.Replace(*TokenKey, *WildcardPair.Value, ESearchCase::IgnoreCase);
    }

    return Formatted;
}

bool UDynamicSocialComponent::TryResolveFastPathDialogue(
    FGameplayTag CategoryTag,
    AActor* TargetActor,
    FString& OutUtterance,
    TSoftObjectPtr<UAnimMontage>& OutMontage,
    FGameplayTag& OutVoiceCueTag,
    FGameplayTag SpecificProfileTag)
{
    return TryResolveFastPathDialogue(CategoryTag, TargetActor, OutUtterance, OutMontage, OutVoiceCueTag, TMap<FString, FString>(), SpecificProfileTag);
}

bool UDynamicSocialComponent::TryResolveFastPathDialogue(
    FGameplayTag CategoryTag,
    AActor* TargetActor,
    FString& OutUtterance,
    TSoftObjectPtr<UAnimMontage>& OutMontage,
    FGameplayTag& OutVoiceCueTag,
    const TMap<FString, FString>& CustomWildcards,
    FGameplayTag SpecificProfileTag)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    const FDynamicRelationshipEdgeItem* TargetEdge = TargetActor ? FindRelationshipWithActor(TargetActor) : nullptr;

    FGameplayTagContainer CurrentMoodTags;
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        CurrentMoodTags.AddTag(Item.NeedTag);
    }

    TArray<const FFastPathDialogueEntry*> ValidEntries;
    float TotalWeight = 0.0f;

    auto EvaluateEntry = [&](const FFastPathDialogueEntry& Entry)
    {
        if (!Entry.CategoryTag.MatchesTag(CategoryTag)) return;

        if (Entry.AllowedMoodTags.Num() > 0 && !CurrentMoodTags.HasAny(Entry.AllowedMoodTags))
        {
            return;
        }

        if (Entry.AllowedRelationshipTypeTags.Num() > 0)
        {
            if (!TargetEdge || !Entry.AllowedRelationshipTypeTags.HasTag(TargetEdge->PrimaryRelationshipTag))
            {
                return;
            }
        }

        bool bAttributesMatch = true;
        for (const FCharacterAttributeRangeDef& Req : Entry.RelationalAttributeRequirements)
        {
            const float CurrentVal = TargetEdge ? GetDecayedRelationshipMetric(TargetEdge->TargetEntityGuid, Req.AttributeTag) : 0.0f;
            if (CurrentVal < Req.MinValue || CurrentVal > Req.MaxValue)
            {
                bAttributesMatch = false;
                break;
            }
        }

        if (!bAttributesMatch) return;

        ValidEntries.Add(&Entry);
        TotalWeight += FMath::Max(0.01f, Entry.SelectionWeight);
    };

    if (SpecificProfileTag.IsValid())
    {
        if (const FFastPathDialogueDef* ProfileDef = GS->FastPathDialogueRegistry.Find(SpecificProfileTag))
        {
            for (const FFastPathDialogueEntry& Entry : ProfileDef->Entries)
            {
                EvaluateEntry(Entry);
            }
        }
    }
    else
    {
        for (const auto& RegistryPair : GS->FastPathDialogueRegistry)
        {
            for (const FFastPathDialogueEntry& Entry : RegistryPair.Value.Entries)
            {
                EvaluateEntry(Entry);
            }
        }
    }

    if (ValidEntries.Num() == 0) return false;

    const float Roll = FMath::FRandRange(0.0f, TotalWeight);
    float Cumulative = 0.0f;
    const FFastPathDialogueEntry* ChosenEntry = ValidEntries[0];

    for (const FFastPathDialogueEntry* Candidate : ValidEntries)
    {
        Cumulative += FMath::Max(0.01f, Candidate->SelectionWeight);
        if (Roll <= Cumulative)
        {
            ChosenEntry = Candidate;
            break;
        }
    }

    FString TemplateText = ChosenEntry->UtteranceTemplate;
    OutMontage = ChosenEntry->GestureMontage;
    OutVoiceCueTag = ChosenEntry->VoiceCueTag;

    if (ChosenEntry->UtteranceVariations.Num() > 0)
    {
        FDynamicDialogueUtteranceDef ChosenVariation;
        float VariationWeight = 0.0f;
        if (SelectBestUtterance(ChosenEntry->UtteranceVariations, TargetActor, ChosenVariation, VariationWeight))
        {
            TemplateText = ChosenVariation.UtteranceText;
            if (ChosenVariation.GestureMontage.IsValid()) OutMontage = ChosenVariation.GestureMontage;
            if (ChosenVariation.VoiceCueTag.IsValid()) OutVoiceCueTag = ChosenVariation.VoiceCueTag;
        }
    }

    OutUtterance = FormatDialogueTokens(TemplateText, TargetActor, TEXT("the area"), TEXT("recently"), CustomWildcards);
    OnFastPathDialogueSpoken.Broadcast(GetOwner(), TargetActor, OutUtterance);
    return true;
}

bool UDynamicSocialComponent::TryResolveDialogueForBehavior(
    const FDynamicAIBehaviorDef& BehaviorDef,
    AActor* TargetActor,
    FString& OutUtterance,
    TSoftObjectPtr<UAnimMontage>& OutMontage,
    FGameplayTag& OutVoiceCueTag)
{
    return TryResolveDialogueForBehavior(BehaviorDef, TargetActor, OutUtterance, OutMontage, OutVoiceCueTag, TMap<FString, FString>());
}

bool UDynamicSocialComponent::TryResolveDialogueForBehavior(
    const FDynamicAIBehaviorDef& BehaviorDef,
    AActor* TargetActor,
    FString& OutUtterance,
    TSoftObjectPtr<UAnimMontage>& OutMontage,
    FGameplayTag& OutVoiceCueTag,
    const TMap<FString, FString>& CustomWildcards)
{
    if (BehaviorDef.Utterances.Num() > 0)
    {
        FDynamicDialogueUtteranceDef ChosenUtterance;
        float OutWeight = 0.0f;
        if (SelectBestUtterance(BehaviorDef.Utterances, TargetActor, ChosenUtterance, OutWeight))
        {
            OutMontage = ChosenUtterance.GestureMontage;
            OutVoiceCueTag = ChosenUtterance.VoiceCueTag;
            OutUtterance = FormatDialogueTokens(ChosenUtterance.UtteranceText, TargetActor, TEXT("the area"), TEXT("recently"), CustomWildcards);
            OnFastPathDialogueSpoken.Broadcast(GetOwner(), TargetActor, OutUtterance);
            return true;
        }
    }

    if (BehaviorDef.DialogueCategoryTag.IsValid())
    {
        if (TryResolveFastPathDialogue(BehaviorDef.DialogueCategoryTag, TargetActor, OutUtterance, OutMontage, OutVoiceCueTag, CustomWildcards, BehaviorDef.DialogueProfileTag))
        {
            return true;
        }
    }

    if (BehaviorDef.UtteranceTemplates.Num() > 0)
    {
        const int32 RandIdx = FMath::RandRange(0, BehaviorDef.UtteranceTemplates.Num() - 1);
        OutUtterance = FormatDialogueTokens(BehaviorDef.UtteranceTemplates[RandIdx], TargetActor, TEXT("the area"), TEXT("recently"), CustomWildcards);
        OnFastPathDialogueSpoken.Broadcast(GetOwner(), TargetActor, OutUtterance);
        return true;
    }

    return false;
}

bool UDynamicSocialComponent::TriggerCalloutToTarget(AActor* TargetActor, float IgnoreTimeout)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetActor) return false;

    ActiveCalloutTarget = TargetActor;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(TimerHandle_CalloutTimeout, this, &UDynamicSocialComponent::HandleCalloutIgnored, IgnoreTimeout, false);
    }
    return true;
}

void UDynamicSocialComponent::HandleCalloutIgnored()
{
    ClearActiveCallout();
}

void UDynamicSocialComponent::ClearActiveCallout()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_CalloutTimeout);
    }
    ActiveCalloutTarget.Reset();
}

// --- Fast Array Lifecycle Implementations ---
void FDynamicRelationshipEdgeItem::PreReplicatedRemove(const FFastRelationshipEdgeArray& InArrayOwner) {}
void FDynamicRelationshipEdgeItem::PostReplicatedAdd(const FFastRelationshipEdgeArray& InArrayOwner) {}
void FDynamicRelationshipEdgeItem::PostReplicatedChange(const FFastRelationshipEdgeArray& InArrayOwner) {}

void FDynamicNeedStateItem::PreReplicatedRemove(const FFastNeedStateArray& InArrayOwner) {}
void FDynamicNeedStateItem::PostReplicatedAdd(const FFastNeedStateArray& InArrayOwner) {}
void FDynamicNeedStateItem::PostReplicatedChange(const FFastNeedStateArray& InArrayOwner) {}

void FDynamicKnownPlaceItem::PreReplicatedRemove(const FFastKnownPlaceArray& InArrayOwner) {}
void FDynamicKnownPlaceItem::PostReplicatedAdd(const FFastKnownPlaceArray& InArrayOwner) {}
void FDynamicKnownPlaceItem::PostReplicatedChange(const FFastKnownPlaceArray& InArrayOwner) {}

void FDynamicAssignedProfessionItem::PreReplicatedRemove(const FFastAssignedProfessionArray& InArrayOwner) {}
void FDynamicAssignedProfessionItem::PostReplicatedAdd(const FFastAssignedProfessionArray& InArrayOwner) {}
void FDynamicAssignedProfessionItem::PostReplicatedChange(const FFastAssignedProfessionArray& InArrayOwner) {}

void FCrimeMemoryItem::PreReplicatedRemove(const FFastCrimeMemoryArray& InArrayOwner) {}
void FCrimeMemoryItem::PostReplicatedAdd(const FFastCrimeMemoryArray& InArrayOwner) {}
void FCrimeMemoryItem::PostReplicatedChange(const FFastCrimeMemoryArray& InArrayOwner) {}

bool UDynamicSocialComponent::GetOwnedPropertyLocation(FVector& OutLocation) const
{
    OutLocation = FVector::ZeroVector;
    if (UWorld* World = GetWorld())
    {
        if (UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>())
        {
            FDynamicPropertyInstance Prop;
            if (BuildSub->GetPropertyOwnedByCharacter(CharacterPersistentGuid, Prop))
            {
                OutLocation = Prop.PropertyBounds.GetCenter();
                return true;
            }
        }
    }
    return false;
}

bool UDynamicSocialComponent::GetOwnedRoomLocation(FGameplayTag RoomTypeTag, FVector& OutLocation) const
{
    OutLocation = FVector::ZeroVector;
    if (UWorld* World = GetWorld())
    {
        if (UDynamicBuildingSubsystem* BuildSub = World->GetSubsystem<UDynamicBuildingSubsystem>())
        {
            FDynamicRoomInstance Room;
            if (BuildSub->GetRoomOwnedByCharacter(CharacterPersistentGuid, RoomTypeTag, Room))
            {
                OutLocation = Room.WorldBounds.GetCenter();
                return true;
            }
        }
    }
    return false;
}

void UDynamicSocialComponent::ServerProcessCrimeSensoryEvent(
    FGameplayTag CrimeTag,
    AActor* Perpetrator,
    AActor* Victim,
    FGuid OptionalStolenItemGuid,
    const TArray<FGuid>& CoConspirators,
    const TMap<FGameplayTag, float>& SensorySignatures)
{
    if (!GetOwner()->HasAuthority() || !Perpetrator) return;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    ADynamicCharacter* OwnerChar = Cast<ADynamicCharacter>(GetOwner());
    if (!OwnerChar) return;

    // 1. Snapshot thread-safe parameters on the GameThread to prevent concurrent GC crashes
    FGameplayTagContainer SourceTags, TargetTags;
    if (UAbilitySystemComponent* ASC = OwnerChar->GetAbilitySystemComponent())
    {
        ASC->GetOwnedGameplayTags(SourceTags);
    }
    if (UAbilitySystemComponent* PerpASC = Perpetrator->FindComponentByClass<UAbilitySystemComponent>())
    {
        PerpASC->GetOwnedGameplayTags(TargetTags);
    }

    TArray<FDynamicAssignedProfessionItem> SafeProfessions = ReplicatedProfessions.Items;
    FGameplayTag SafeDefaultMovementState = OwnerChar->DefaultMovementStateTag;
    FGameplayTag SafeActiveMovementState = OwnerChar->ActiveMovementStateTag;
    
    TWeakObjectPtr<ADynamicCharacter> WeakOwner(OwnerChar);
    TWeakObjectPtr<UAbilitySystemComponent> WeakSourceASC(OwnerChar->GetAbilitySystemComponent());
    TWeakObjectPtr<UAbilitySystemComponent> WeakTargetASC(Perpetrator->FindComponentByClass<UAbilitySystemComponent>());

    // 2. Offload heavily iterative logic and data-driven condition tree processing to the ThreadPool
    Async(EAsyncExecution::ThreadPool, [GS, SafeProfessions, SafeDefaultMovementState, SafeActiveMovementState, SensorySignatures, SourceTags, TargetTags, WeakOwner, WeakSourceASC, WeakTargetASC]() 
    {
        FGameplayTag TriggeredBehaviorTag;

        for (const auto& SigPair : SensorySignatures)
        {
            const FGameplayTag StimulusTag = SigPair.Key;
            
            // Priority A: Check profession-specific sensory responses (e.g. Guard reacts to stealth footsteps)
            for (const FDynamicAssignedProfessionItem& ProfItem : SafeProfessions)
            {
                if (const FDynamicProfessionDef* ProfDef = GS->ProfessionMasterRegistry.Find(ProfItem.ProfessionTag))
                {
                    for (const FSensoryResponseRule& Rule : ProfDef->SensoryResponses)
                    {
                        if (Rule.TriggeringStimulusTag == StimulusTag)
                        {
                            bool bPassesCondition = true;
                            if (!Rule.PrerequisiteConditionTree.IsNone())
                            {
                                if (const FDynamicConditionTreeContainer* Tree = GS->SharedConditionTrees.Find(Rule.PrerequisiteConditionTree))
                                {
                                    bPassesCondition = UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, WeakSourceASC.Get(), WeakTargetASC.Get(), SourceTags, TargetTags);
                                }
                                else
                                {
                                    bPassesCondition = false;
                                }
                            }
                            
                            if (bPassesCondition)
                            {
                                TriggeredBehaviorTag = Rule.ResultingBehaviorTag;
                                break;
                            }
                        }
                    }
                }
                if (TriggeredBehaviorTag.IsValid()) break;
            }

            // Priority B: Fall back to foundational AI Agent Template responses (e.g. Civilian flees from explosion)
            if (!TriggeredBehaviorTag.IsValid())
            {
                if (const FDynamicAIAgentDef* AgentDef = GS->MasterAIAgentRegistry.Find(SafeDefaultMovementState))
                {
                    for (const FSensoryResponseRule& Rule : AgentDef->SensoryResponses)
                    {
                        if (Rule.TriggeringStimulusTag == StimulusTag)
                        {
                            TriggeredBehaviorTag = Rule.ResultingBehaviorTag;
                            break;
                        }
                    }
                }
            }

            if (TriggeredBehaviorTag.IsValid()) break;
        }

        // 3. Dispatch the resolved behavior securely back to the GameThread for execution
        if (TriggeredBehaviorTag.IsValid())
        {
            Async(EAsyncExecution::TaskGraphMainThread, [WeakOwner, TriggeredBehaviorTag, SafeActiveMovementState]() 
            {
                if (ADynamicCharacter* StrongOwner = WeakOwner.Get())
                {
                    if (UDynamicQuestComponent* QuestComp = StrongOwner->FindComponentByClass<UDynamicQuestComponent>())
                    {
                        // Suspend current long-term work/travel goal to handle the urgent stimuli
                        QuestComp->PushUrgentGoalBehavior(SafeActiveMovementState);
                    }
                    
                    StrongOwner->ServerSetMovementState(TriggeredBehaviorTag);
                }
            });
        }
    });
}

void UDynamicSocialComponent::ExecuteDynamicWitnessReaction(const FCrimeMemoryItem& Memory, AActor* Perpetrator, AActor* Victim)
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicCrimeDef* CrimeDef = GS->CrimeMasterRegistry.Find(Memory.CrimeTag);
    if (!CrimeDef || CrimeDef->AssociatedWitnessBehaviors.Num() == 0) return;
    
    FGameplayTag SelectedBehavior = FGameplayTag::EmptyTag;
    float BestWeight = -MAX_flt;
    
    UAbilitySystemComponent* ASC = GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
    FGameplayTagContainer SourceTags, TargetTags;
    if (ASC) ASC->GetOwnedGameplayTags(SourceTags);

    for (const FGameplayTag& BehaviorTag : CrimeDef->AssociatedWitnessBehaviors)
    {
        if (const FDynamicAIBehaviorDef* BehaviorDef = GS->AIBehaviorMasterRegistry.Find(BehaviorTag))
        {
            bool bPasses = true;
            if (!BehaviorDef->PrerequisiteConditionTreeName.IsNone())
            {
                if (const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(GetOwner(), BehaviorDef->PrerequisiteConditionTreeName))
                {
                    bPasses = UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, SourceTags, TargetTags);
                }
            }

            if (bPasses && BehaviorDef->BasePriority > BestWeight)
            {
                BestWeight = BehaviorDef->BasePriority;
                SelectedBehavior = BehaviorTag;
            }
        }
    }

    if (SelectedBehavior.IsValid() && ASC)
    {
        ASC->AddLooseGameplayTags(FGameplayTagContainer(SelectedBehavior));

        if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
        {
            if (AAIController* AICon = Cast<AAIController>(PawnOwner->GetController()))
            {
                if (UBlackboardComponent* BB = AICon->GetBlackboardComponent())
                {
                    if (Perpetrator) BB->SetValueAsObject(FName("ThreatActor"), Perpetrator);
                    if (Victim) BB->SetValueAsObject(FName("DefendTargetActor"), Victim);
                }
            }
        }
    }
}

bool UDynamicSocialComponent::ServerReportCrimesToAuthority(AActor* AuthorityFigure)
{
    if (!GetOwner()->HasAuthority() || !AuthorityFigure) return false;

    UDynamicSocialComponent* AuthoritySocial = AuthorityFigure->FindComponentByClass<UDynamicSocialComponent>();
    if (!AuthoritySocial) return false;

    bool bReportedAny = false;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;

    for (FCrimeMemoryItem& Memory : ReplicatedCrimeMemories.Items)
    {
        if (!Memory.bHasBeenReportedToAuthority)
        {
            if (GS && AuthoritySocial->AssignedJurisdictionTag.IsValid())
            {
                GS->ServerRegisterCrimeWithJurisdiction(AuthoritySocial->AssignedJurisdictionTag, Memory);
            }
            else
            {
                FCrimeMemoryItem SharedMemory = Memory;
                SharedMemory.MemoryID = FGuid::NewGuid();
                SharedMemory.bHasBeenReportedToAuthority = true;
                
                FCrimeMemoryItem& Added = AuthoritySocial->ReplicatedCrimeMemories.Items.Add_GetRef(SharedMemory);
                AuthoritySocial->ReplicatedCrimeMemories.MarkItemDirty(Added);
            }

            Memory.bHasBeenReportedToAuthority = true;
            ReplicatedCrimeMemories.MarkItemDirty(Memory);
            bReportedAny = true;
        }
    }
    
    return bReportedAny;
}

bool UDynamicSocialComponent::ServerSearchTargetForContraband(ADynamicCharacter* Suspect)
{
    if (!GetOwner()->HasAuthority() || !Suspect || !AssignedJurisdictionTag.IsValid()) return false;

    UDynamicSocialComponent* SuspectSocial = Suspect->FindComponentByClass<UDynamicSocialComponent>();
    if (!SuspectSocial) return false;

    // Search is conducted based on whether THIS guard perceives them as suspicious/wanted, not the global ledger
    const float PerceivedBounty = GetPerceivedBounty(SuspectSocial->CharacterPersistentGuid, AssignedJurisdictionTag);
    
    // We can also allow random searches if the guard has the "RandomSearch" behavior active, but primarily:
    bool bFoundStolenGoods = false;

    auto CheckOwnerGuid = [&](const FGuid& ItemOwner) {
        if (ItemOwner.IsValid() && ItemOwner != SuspectSocial->CharacterPersistentGuid)
        {
            bFoundStolenGoods = true;
        }
    };

    if (UDynamicContainerComponent* Container = Suspect->FindComponentByClass<UDynamicContainerComponent>())
    {
        for (const FDynamicContainerSlotItem& Slot : Container->GetReplicatedSlots().Items)
        {
            if (Slot.StackCount > 0 && Slot.StoredActorArchetypeTag.IsValid())
            {
                CheckOwnerGuid(Slot.OwnerGuid);
                if (bFoundStolenGoods) break;
            }
        }
    }

    if (bFoundStolenGoods)
    {
        // Guard generates a new memory of the theft, rather than instantly updating the global ledger
        FCrimeMemoryItem NewMemory;
        NewMemory.MemoryID = FGuid::NewGuid();
        NewMemory.CrimeTag = FGameplayTag::RequestGameplayTag(TEXT("Crime.Type.Theft"), false);
        NewMemory.PerpetratorGuid = SuspectSocial->CharacterPersistentGuid;
        NewMemory.bPerpetratorKnown = true;
        NewMemory.Timestamp = GetWorld()->GetTimeSeconds();
        NewMemory.IdentificationConfidence = 1.0f;
        NewMemory.bHasBeenReportedToAuthority = false;

        FCrimeMemoryItem& Added = ReplicatedCrimeMemories.Items.Add_GetRef(NewMemory);
        ReplicatedCrimeMemories.MarkItemDirty(Added);
        return true;
    }

    return false;
}

void UDynamicSocialComponent::ServerEvaluateAndBroadcastAction(
    FGameplayTag ActionTag,
    AActor* Perpetrator,
    AActor* Victim,
    FGuid OptionalStolenItemGuid,
    const TArray<FGuid>& CoConspirators)
{
    if (!GetOwner()->HasAuthority() || !ActionTag.IsValid() || !Perpetrator) return;
    
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicJurisdictionDef* JurisDef = GS->JurisdictionMasterRegistry.Find(AssignedJurisdictionTag);
    if (!JurisDef) return;

    FGameplayTag CrimeTag = FGameplayTag::EmptyTag;
    for (const FTagTagPair& Pair : JurisDef->ActionToCrimeMap.ReplicatedEntries)
    {
        if (Pair.KeyTag == ActionTag)
        {
            CrimeTag = Pair.ValueTag;
            break;
        }
    }

    if (CrimeTag.IsValid())
    {
        // EXEMPTION CHECK: Check if the Guard/Profession has a legal exemption for this crime via a Condition Tree
        bool bIsExempt = false;
        if (const FName* ExemptionTreeName = JurisDef->LegalExemptionTrees.Find(CrimeTag))
        {
            const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(Perpetrator, *ExemptionTreeName);
            if (Tree)
            {
                UAbilitySystemComponent* SourceASC = Perpetrator->FindComponentByClass<UAbilitySystemComponent>();
                UAbilitySystemComponent* TargetASC = Victim ? Victim->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
                
                FGameplayTagContainer SourceTags, TargetTags;
                if (SourceASC) SourceASC->GetOwnedGameplayTags(SourceTags);
                if (TargetASC) TargetASC->GetOwnedGameplayTags(TargetTags);

                if (UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, SourceASC, TargetASC, SourceTags, TargetTags))
                {
                    bIsExempt = true; // e.g. A Guard killing a Wanted murderer is exempt from Crime.Type.Murder
                }
            }
        }

        if (!bIsExempt)
        {
            ServerProcessCrimeSensoryEvent(CrimeTag, Perpetrator, Victim, OptionalStolenItemGuid, CoConspirators, TMap<FGameplayTag, float>());
        }
    }
}

bool UDynamicSocialComponent::ServerInterrogateSuspect(ADynamicCharacter* Suspect, FName InterrogationConditionTree)
{
    if (!GetOwner()->HasAuthority() || !Suspect) return false;

    const FGameplayTag AuthorizedForce = GetAuthorizedForceLevel(Suspect, AssignedJurisdictionTag);
    static const FGameplayTag LethalForceTag = FGameplayTag::RequestGameplayTag(TEXT("ForceLevel.Lethal"), false);

    return AuthorizedForce.MatchesTag(LethalForceTag);
}

float UDynamicSocialComponent::GetPerceivedBounty(const FGuid& SuspectGuid, FGameplayTag JurisdictionTag) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return 0.0f;

    const FDynamicJurisdictionDef* JurDef = GS->JurisdictionMasterRegistry.Find(JurisdictionTag);
    if (!JurDef) return 0.0f;

    float TotalBounty = 0.0f;
    for (const FCrimeMemoryItem& Memory : ReplicatedCrimeMemories.Items)
    {
        if (Memory.PerpetratorGuid == SuspectGuid)
        {
            if (const FCrimeSeverityOverride* Law = JurDef->LocalLaws.Find(Memory.CrimeTag))
            {
                if (Law->bIsIllegal) TotalBounty += Law->SeverityOverride;
            }
            else if (const FDynamicCrimeDef* BaseCrime = GS->CrimeMasterRegistry.Find(Memory.CrimeTag))
            {
                TotalBounty += BaseCrime->BaseSeverity;
            }
        }
    }
    return TotalBounty;
}

FGameplayTag UDynamicSocialComponent::GetAuthorizedForceLevel(ADynamicCharacter* Suspect, FGameplayTag JurisdictionTag) const
{
    if (!Suspect) return FGameplayTag::EmptyTag;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGameplayTag::EmptyTag;

    const FDynamicJurisdictionDef* JurDef = GS->JurisdictionMasterRegistry.Find(JurisdictionTag);
    if (!JurDef) return FGameplayTag::EmptyTag;

    FGameplayTag BestForceLevel = FGameplayTag::RequestGameplayTag(TEXT("ForceLevel.None"), false);
    float HighestPriority = -MAX_flt;

    UAbilitySystemComponent* SourceASC = GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
    UAbilitySystemComponent* TargetASC = Suspect->GetAbilitySystemComponent();
    
    FGameplayTagContainer SourceTags, TargetTags;
    if (SourceASC) SourceASC->GetOwnedGameplayTags(SourceTags);
    if (TargetASC) TargetASC->GetOwnedGameplayTags(TargetTags);

    for (const FEscalationOfForceRule& Rule : JurDef->EscalationOfForceRules)
    {
        if (Rule.Priority > HighestPriority)
        {
            if (!Rule.EvaluationConditionTree.IsNone())
            {
                const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(GetOwner(), Rule.EvaluationConditionTree);
                if (Tree && UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, SourceASC, TargetASC, SourceTags, TargetTags))
                {
                    HighestPriority = Rule.Priority;
                    BestForceLevel = Rule.AuthorizedForceLevel;
                }
            }
        }
    }

    return BestForceLevel;
}