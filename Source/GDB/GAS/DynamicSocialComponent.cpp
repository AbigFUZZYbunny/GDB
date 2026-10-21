// DynamicSocialComponent.cpp
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicLifeComponent.h"
#include "DynamicCloudAISubsystem.h"
#include "DynamicGASManagerComponent.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonSerializer.h"
#include "JsonObjectConverter.h"

UDynamicSocialComponent::UDynamicSocialComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);

    CharacterPersistentGuid = FGuid::NewGuid();
    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
    ReplicatedProfessions.OwningSocialComp = this;
}

void UDynamicSocialComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
    ReplicatedProfessions.OwningSocialComp = this;

    RebuildRelationshipCache();
    RebuildPlaceCache();
    RebuildNeedCache();
}

void UDynamicSocialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicSocialComponent, CharacterPersistentGuid);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedProfessions);
    DOREPLIFETIME(UDynamicSocialComponent, PersonalityProfile);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedRelationships);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedKnownPlaces);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedNeedsAndMoods);
    DOREPLIFETIME(UDynamicSocialComponent, bIsSuspendedForCloudAI);
}

void FDynamicRelationshipEdgeItem::PostReplicatedAdd(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->HandleRelationshipEdgeReplicated(*this, false);
    }
}

void FDynamicRelationshipEdgeItem::PostReplicatedChange(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->HandleRelationshipEdgeReplicated(*this, false);
    }
}

void FDynamicNeedStateItem::PreReplicatedRemove(const FFastNeedStateArray& InArrayOwner) {}
void FDynamicNeedStateItem::PostReplicatedAdd(const FFastNeedStateArray& InArrayOwner)
{
    LocalExactValue = DynamicQuantization::DequantizeScale100(PackedCurrentValue);
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleNeedStateReplicated(*this, false);
}
void FDynamicNeedStateItem::PostReplicatedChange(const FFastNeedStateArray& InArrayOwner)
{
    LocalExactValue = DynamicQuantization::DequantizeScale100(PackedCurrentValue);
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleNeedStateReplicated(*this, false);
}

void FDynamicAssignedProfessionItem::PreReplicatedRemove(const FFastAssignedProfessionArray& InArrayOwner) {}
void FDynamicAssignedProfessionItem::PostReplicatedAdd(const FFastAssignedProfessionArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleProfessionReplicated(*this, false);
}
void FDynamicAssignedProfessionItem::PostReplicatedChange(const FFastAssignedProfessionArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleProfessionReplicated(*this, false);
}

void FDynamicKnownPlaceItem::PreReplicatedRemove(const FFastKnownPlaceArray& InArrayOwner) {}
void FDynamicKnownPlaceItem::PostReplicatedAdd(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleKnownPlaceReplicated(*this, false);
}
void FDynamicKnownPlaceItem::PostReplicatedChange(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->HandleKnownPlaceReplicated(*this, false);
}

void UDynamicSocialComponent::OnRep_PersonalityProfile() {}
void UDynamicSocialComponent::OnRep_RelationshipEdges() { RebuildRelationshipCache(); }
void UDynamicSocialComponent::OnRep_KnownPlaces() { RebuildPlaceCache(); }
void UDynamicSocialComponent::OnRep_NeedStates() { RebuildNeedCache(); }
void UDynamicSocialComponent::OnRep_Professions() {}
void UDynamicSocialComponent::OnRep_IsSuspendedForCloudAI()
{
    OnSuspensionStateChanged.Broadcast(bIsSuspendedForCloudAI);
}

void UDynamicSocialComponent::HandleProfessionReplicated(const FDynamicAssignedProfessionItem& Item, bool bRemoved) {}
void UDynamicSocialComponent::HandleKnownPlaceReplicated(const FDynamicKnownPlaceItem& Item, bool bRemoved) { RebuildPlaceCache(); }
void UDynamicSocialComponent::HandleNeedStateReplicated(const FDynamicNeedStateItem& Item, bool bRemoved) { RebuildNeedCache(); }

void UDynamicSocialComponent::HandleRelationshipEdgeReplicated(const FDynamicRelationshipEdgeItem& Item, bool bRemoved)
{
    // Client-side unpacking of metrics based on the JSON relationship schema
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (GS)
    {
        if (const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(Item.PrimaryRelationshipTag))
        {
            FDynamicRelationshipEdgeItem& MutableItem = const_cast<FDynamicRelationshipEdgeItem&>(Item);
            for (FDynamicRelationshipMetricEntry& MetricEntry : MutableItem.Metrics)
            {
                if (const FRelationshipMetricRule* Rule = RelDef->FindMetricRule(MetricEntry.MetricTag))
                {
                    MetricEntry.ExactValue = DynamicQuantization::DequantizeRange(MetricEntry.PackedValue, Rule->MinValue, Rule->MaxValue);
                }
            }
        }
    }
    RebuildRelationshipCache();
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

float UDynamicSocialComponent::EvaluateMetricDecay(float BaseVal, const FRelationshipMetricRule& Rule, double ElapsedSeconds) const
{
    if (Rule.DecayCurve == ERelationshipDecayCurve::None || FMath::IsNearlyZero(Rule.DecayRatePerDay))
    {
        return BaseVal;
    }

    const float DaysElapsed = static_cast<float>(ElapsedSeconds / SecondsPerGameDay);
    if (DaysElapsed <= 0.0f)
    {
        return BaseVal;
    }

    const float EffectiveRate = Rule.DecayRatePerDay * Rule.VolatilityScalar;
    float DecayedVal = BaseVal;

    switch (Rule.DecayCurve)
    {
        case ERelationshipDecayCurve::Linear:
        {
            if (BaseVal > Rule.DecayTargetFloor)
            {
                DecayedVal = FMath::Max(Rule.DecayTargetFloor, BaseVal - (EffectiveRate * DaysElapsed));
            }
            else if (BaseVal < Rule.DecayTargetFloor)
            {
                DecayedVal = FMath::Min(Rule.DecayTargetFloor, BaseVal + (EffectiveRate * DaysElapsed));
            }
            break;
        }
        case ERelationshipDecayCurve::Exponential:
        {
            const float DeltaFromFloor = BaseVal - Rule.DecayTargetFloor;
            DecayedVal = Rule.DecayTargetFloor + (DeltaFromFloor * FMath::Exp(-EffectiveRate * DaysElapsed));
            break;
        }
        case ERelationshipDecayCurve::StepThreshold:
        {
            const int32 Steps = FMath::FloorToInt(DaysElapsed);
            if (BaseVal > Rule.DecayTargetFloor)
            {
                DecayedVal = FMath::Max(Rule.DecayTargetFloor, BaseVal - (EffectiveRate * Steps));
            }
            else if (BaseVal < Rule.DecayTargetFloor)
            {
                DecayedVal = FMath::Min(Rule.DecayTargetFloor, BaseVal + (EffectiveRate * Steps));
            }
            break;
        }
        default:
            break;
    }

    if (Rule.bClampToMinMax)
    {
        DecayedVal = FMath::Clamp(DecayedVal, Rule.MinValue, Rule.MaxValue);
    }

    return DecayedVal;
}

float UDynamicSocialComponent::GetDecayedRelationshipMetric(const FGuid& TargetGuid, FGameplayTag MetricTag) const
{
    if (!TargetGuid.IsValid() || !MetricTag.IsValid()) return 0.0f;

    const int32* FoundIdxPtr = GuidToEdgeIndexMap.Find(TargetGuid);
    if (!FoundIdxPtr || !ReplicatedRelationships.Items.IsValidIndex(*FoundIdxPtr)) return 0.0f;

    const FDynamicRelationshipEdgeItem& Edge = ReplicatedRelationships.Items[*FoundIdxPtr];

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return Edge.GetMetricValue(MetricTag);

    const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(Edge.PrimaryRelationshipTag);
    if (!RelDef) return Edge.GetMetricValue(MetricTag);

    const FRelationshipMetricRule* Rule = RelDef->FindMetricRule(MetricTag);
    if (!Rule) return Edge.GetMetricValue(MetricTag);

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const double ElapsedSecs = FMath::Max(0.0, Now - Edge.LastInteractionWorldTime);

    return EvaluateMetricDecay(Edge.GetMetricValue(MetricTag), *Rule, ElapsedSecs);
}

TArray<FTagFloatPair> UDynamicSocialComponent::GetAllDecayedRelationshipMetrics(const FGuid& TargetGuid) const
{
    TArray<FTagFloatPair> Results;
    if (!TargetGuid.IsValid()) return Results;

    const int32* FoundIdxPtr = GuidToEdgeIndexMap.Find(TargetGuid);
    if (!FoundIdxPtr || !ReplicatedRelationships.Items.IsValidIndex(*FoundIdxPtr)) return Results;

    const FDynamicRelationshipEdgeItem& Edge = ReplicatedRelationships.Items[*FoundIdxPtr];
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FDynamicRelationshipTypeDef* RelDef = GS ? GS->RelationshipTypeRegistry.Find(Edge.PrimaryRelationshipTag) : nullptr;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const double ElapsedSecs = FMath::Max(0.0, Now - Edge.LastInteractionWorldTime);

    for (const FDynamicRelationshipMetricEntry& Entry : Edge.Metrics)
    {
        FTagFloatPair Pair;
        Pair.KeyTag = Entry.MetricTag;
        if (RelDef)
        {
            if (const FRelationshipMetricRule* Rule = RelDef->FindMetricRule(Entry.MetricTag))
            {
                Pair.Value = EvaluateMetricDecay(Entry.ExactValue, *Rule, ElapsedSecs);
            }
            else
            {
                Pair.Value = Entry.ExactValue;
            }
        }
        else
        {
            Pair.Value = Entry.ExactValue;
        }
        Results.Add(Pair);
    }
    return Results;
}

FGameplayTag UDynamicSocialComponent::GetRelationshipType(const FGuid& TargetGuid) const
{
    if (const int32* FoundIdxPtr = GuidToEdgeIndexMap.Find(TargetGuid))
    {
        if (ReplicatedRelationships.Items.IsValidIndex(*FoundIdxPtr))
        {
            return ReplicatedRelationships.Items[*FoundIdxPtr].PrimaryRelationshipTag;
        }
    }
    return FGameplayTag::EmptyTag;
}

void UDynamicSocialComponent::ServerRecordSocialInteraction(
    const FGuid& TargetGuid,
    AActor* OptionalTargetActor,
    ERelationshipTargetKind TargetKind,
    FGameplayTag RelationshipTag,
    const TArray<FTagFloatPair>& MetricDeltas,
    const FGameplayTagContainer& AddedContextTags)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetGuid.IsValid()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    int32* FoundIdxPtr = GuidToEdgeIndexMap.Find(TargetGuid);

    if (FoundIdxPtr && ReplicatedRelationships.Items.IsValidIndex(*FoundIdxPtr))
    {
        FDynamicRelationshipEdgeItem& Edge = ReplicatedRelationships.Items[*FoundIdxPtr];
        const FGameplayTag OldType = Edge.PrimaryRelationshipTag;

        if (RelationshipTag.IsValid() && Edge.PrimaryRelationshipTag != RelationshipTag)
        {
            Edge.PrimaryRelationshipTag = RelationshipTag;
            OnRelationshipTypeChanged.Broadcast(TargetGuid, OldType, RelationshipTag);
        }

        const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(Edge.PrimaryRelationshipTag);
        if (!RelDef) return;

        bool bEdgeModified = false;
        const double ElapsedSecs = FMath::Max(0.0, CurrentWorldTime - Edge.LastInteractionWorldTime);

        for (const FTagFloatPair& Delta : MetricDeltas)
        {
            if (!Delta.KeyTag.IsValid()) continue;
            const FRelationshipMetricRule* Rule = RelDef->FindMetricRule(Delta.KeyTag);
            if (!Rule) continue;

            const float CurrentDecayedVal = EvaluateMetricDecay(Edge.GetMetricValue(Delta.KeyTag), *Rule, ElapsedSecs);
            float NewVal = CurrentDecayedVal + Delta.Value;
            if (Rule->bClampToMinMax)
            {
                NewVal = FMath::Clamp(NewVal, Rule->MinValue, Rule->MaxValue);
            }

            const uint8 NewPacked = DynamicQuantization::QuantizeRange(NewVal, Rule->MinValue, Rule->MaxValue);
            uint8 OldPacked = 0;
            for (const FDynamicRelationshipMetricEntry& MEntry : Edge.Metrics)
            {
                if (MEntry.MetricTag == Delta.KeyTag)
                {
                    OldPacked = MEntry.PackedValue;
                    break;
                }
            }

            if (OldPacked != NewPacked)
            {
                bEdgeModified = true;
            }

            Edge.SetMetricValue(Delta.KeyTag, NewVal, NewPacked);
            OnRelationshipMetricChanged.Broadcast(TargetGuid, Edge.PrimaryRelationshipTag, Delta.KeyTag, NewVal);
        }

        Edge.LastInteractionWorldTime = CurrentWorldTime;
        Edge.SecondaryContextTags.AppendTags(AddedContextTags);

        if (bEdgeModified)
        {
            ReplicatedRelationships.MarkItemDirty(Edge);
        }
        return;
    }

    // Initialize new edge from JSON schema rules
    const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(RelationshipTag);
    if (!RelDef) return;

    FDynamicRelationshipEdgeItem NewEdge;
    NewEdge.TargetEntityGuid = TargetGuid;
    NewEdge.CachedTargetActor = OptionalTargetActor;
    NewEdge.TargetKind = TargetKind;
    NewEdge.PrimaryRelationshipTag = RelationshipTag;
    NewEdge.SecondaryContextTags = AddedContextTags;
    NewEdge.LastInteractionWorldTime = CurrentWorldTime;

    // Pre-populate all metrics supported by this relationship type from schema defaults
    for (const FRelationshipMetricRule& Rule : RelDef->SupportedMetrics)
    {
        float InitialVal = Rule.DefaultValue;
        for (const FTagFloatPair& Delta : MetricDeltas)
        {
            if (Delta.KeyTag == Rule.MetricTag)
            {
                InitialVal += Delta.Value;
                break;
            }
        }
        if (Rule.bClampToMinMax)
        {
            InitialVal = FMath::Clamp(InitialVal, Rule.MinValue, Rule.MaxValue);
        }

        const uint8 Packed = DynamicQuantization::QuantizeRange(InitialVal, Rule.MinValue, Rule.MaxValue);
        NewEdge.SetMetricValue(Rule.MetricTag, InitialVal, Packed);
    }

    FDynamicRelationshipEdgeItem& Added = ReplicatedRelationships.Items.Add_GetRef(NewEdge);
    ReplicatedRelationships.MarkItemDirty(Added);
    RebuildRelationshipCache();

    for (const FDynamicRelationshipMetricEntry& Entry : Added.Metrics)
    {
        OnRelationshipMetricChanged.Broadcast(TargetGuid, RelationshipTag, Entry.MetricTag, Entry.ExactValue);
    }
}

void UDynamicSocialComponent::ServerModifyRelationshipMetric(const FGuid& TargetGuid, FGameplayTag MetricTag, float DeltaAmount)
{
    TArray<FTagFloatPair> Deltas;
    FTagFloatPair Pair;
    Pair.KeyTag = MetricTag;
    Pair.Value = DeltaAmount;
    Deltas.Add(Pair);
    ServerRecordSocialInteraction(TargetGuid, nullptr, ERelationshipTargetKind::Character, FGameplayTag::EmptyTag, Deltas, FGameplayTagContainer());
}

void UDynamicSocialComponent::ServerPruneDeadRelationships()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    int32 RemovedCount = ReplicatedRelationships.Items.RemoveAll([this, GS, Now](const FDynamicRelationshipEdgeItem& Edge)
    {
        const FDynamicRelationshipTypeDef* RelDef = GS->RelationshipTypeRegistry.Find(Edge.PrimaryRelationshipTag);
        if (!RelDef || !RelDef->bPruneWhenAllAtFloor) return false;

        const double ElapsedSecs = FMath::Max(0.0, Now - Edge.LastInteractionWorldTime);
        for (const FRelationshipMetricRule& Rule : RelDef->SupportedMetrics)
        {
            const float DecayedVal = EvaluateMetricDecay(Edge.GetMetricValue(Rule.MetricTag), Rule, ElapsedSecs);
            if (!FMath::IsNearlyEqual(DecayedVal, Rule.DecayTargetFloor, RelDef->PruneTolerance))
            {
                return false; // Still holds an active value above floor
            }
        }
        return true; // All metrics returned to floor; safely prune
    });

    if (RemovedCount > 0)
    {
        ReplicatedRelationships.MarkArrayDirty();
        RebuildRelationshipCache();
    }
}

bool UDynamicSocialComponent::ServerSeverRelationship(const FGuid& TargetGuid)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetGuid.IsValid()) return false;

    const int32* FoundIdx = GuidToEdgeIndexMap.Find(TargetGuid);
    if (!FoundIdx || !ReplicatedRelationships.Items.IsValidIndex(*FoundIdx)) return false;

    const FGameplayTag SeveredTag = ReplicatedRelationships.Items[*FoundIdx].PrimaryRelationshipTag;
    ReplicatedRelationships.Items.RemoveAtSwap(*FoundIdx);
    ReplicatedRelationships.MarkArrayDirty();
    RebuildRelationshipCache();

    OnRelationshipTypeChanged.Broadcast(TargetGuid, SeveredTag, FGameplayTag::EmptyTag);
    return true;
}

void UDynamicSocialComponent::ServerPruneStalePlaces(float MinimumConfidenceThreshold)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    int32 RemovedCount = ReplicatedKnownPlaces.Items.RemoveAll([MinimumConfidenceThreshold](const FDynamicKnownPlaceItem& Place)
    {
        return Place.ConfidenceScore <= MinimumConfidenceThreshold;
    });

    if (RemovedCount > 0)
    {
        ReplicatedKnownPlaces.MarkArrayDirty();
        RebuildPlaceCache();
    }

    EnforceKnownPlaceMemoryCap();
}

bool UDynamicSocialComponent::ServerForgetPlace(FGameplayTag PlaceRoleTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !PlaceRoleTag.IsValid()) return false;

    const int32* FoundIdx = RoleToPlaceIndexMap.Find(PlaceRoleTag);
    if (!FoundIdx || !ReplicatedKnownPlaces.Items.IsValidIndex(*FoundIdx)) return false;

    ReplicatedKnownPlaces.Items.RemoveAtSwap(*FoundIdx);
    ReplicatedKnownPlaces.MarkArrayDirty();
    RebuildPlaceCache();
    return true;
}

bool UDynamicSocialComponent::ServerClearNeed(FGameplayTag NeedTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NeedTag.IsValid()) return false;

    const int32* FoundIdx = TagToNeedIndexMap.Find(NeedTag);
    if (!FoundIdx || !ReplicatedNeedsAndMoods.Items.IsValidIndex(*FoundIdx)) return false;

    ReplicatedNeedsAndMoods.Items.RemoveAtSwap(*FoundIdx);
    ReplicatedNeedsAndMoods.MarkArrayDirty();
    RebuildNeedCache();
    OnNeedOrMoodChanged.Broadcast(NeedTag, 0.0f);
    return true;
}

void UDynamicSocialComponent::ServerRegisterKnownPlace(FGameplayTag PlaceRoleTag, FGameplayTag LandmarkOrArchetypeTag, const FGuid& ActorGuid, const FVector& WorldLocation, float Confidence)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !PlaceRoleTag.IsValid()) return;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const int32* FoundIdx = RoleToPlaceIndexMap.Find(PlaceRoleTag);

    if (FoundIdx && ReplicatedKnownPlaces.Items.IsValidIndex(*FoundIdx))
    {
        FDynamicKnownPlaceItem& Place = ReplicatedKnownPlaces.Items[*FoundIdx];
        Place.PlaceArchetypeOrLandmarkTag = LandmarkOrArchetypeTag;
        Place.LinkedActorGuid = ActorGuid;
        Place.LastKnownLocation = WorldLocation;
        Place.ConfidenceScore = Confidence;
        Place.LastVisitedWorldTime = Now;
        ReplicatedKnownPlaces.MarkItemDirty(Place);
        return;
    }

    FDynamicKnownPlaceItem NewPlace;
    NewPlace.PlaceRoleTag = PlaceRoleTag;
    NewPlace.PlaceArchetypeOrLandmarkTag = LandmarkOrArchetypeTag;
    NewPlace.LinkedActorGuid = ActorGuid;
    NewPlace.LastKnownLocation = WorldLocation;
    NewPlace.ConfidenceScore = Confidence;
    NewPlace.LastVisitedWorldTime = Now;

    FDynamicKnownPlaceItem& Added = ReplicatedKnownPlaces.Items.Add_GetRef(NewPlace);
    ReplicatedKnownPlaces.MarkItemDirty(Added);
    RebuildPlaceCache();

    EnforceKnownPlaceMemoryCap();
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
    if (const int32* FoundIdx = TagToNeedIndexMap.Find(NeedOrMoodTag))
    {
        if (ReplicatedNeedsAndMoods.Items.IsValidIndex(*FoundIdx))
        {
            return ReplicatedNeedsAndMoods.Items[*FoundIdx].LocalExactValue;
        }
    }
    return 100.0f;
}

void UDynamicSocialComponent::ServerModifyNeedOrMood(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NeedOrMoodTag.IsValid()) return;

    const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const int32* FoundIdxPtr = TagToNeedIndexMap.Find(NeedOrMoodTag);

    if (FoundIdxPtr && ReplicatedNeedsAndMoods.Items.IsValidIndex(*FoundIdxPtr))
    {
        FDynamicNeedStateItem& Item = ReplicatedNeedsAndMoods.Items[*FoundIdxPtr];
        Item.LocalExactValue = FMath::Clamp(Item.LocalExactValue + BaseDelta, 0.0f, 100.0f);
        Item.LastEvaluatedWorldTime = CurrentWorldTime;

        const uint8 NewPackedValue = DynamicQuantization::QuantizeScale100(Item.LocalExactValue);
        if (Item.PackedCurrentValue != NewPackedValue)
        {
            Item.PackedCurrentValue = NewPackedValue;
            ReplicatedNeedsAndMoods.MarkItemDirty(Item);
        }

        OnNeedOrMoodChanged.Broadcast(NeedOrMoodTag, Item.LocalExactValue);
        return;
    }

    FDynamicNeedStateItem NewNeed;
    NewNeed.NeedOrMoodTag = NeedOrMoodTag;
    NewNeed.LocalExactValue = FMath::Clamp(100.0f + BaseDelta, 0.0f, 100.0f);
    NewNeed.PackedCurrentValue = DynamicQuantization::QuantizeScale100(NewNeed.LocalExactValue);
    NewNeed.LastEvaluatedWorldTime = CurrentWorldTime;

    FDynamicNeedStateItem& Added = ReplicatedNeedsAndMoods.Items.Add_GetRef(NewNeed);
    ReplicatedNeedsAndMoods.MarkItemDirty(Added);
    RebuildNeedCache();
    OnNeedOrMoodChanged.Broadcast(NeedOrMoodTag, NewNeed.LocalExactValue);
}

void UDynamicSocialComponent::ServerModifyNeedOrMoodBP(FGameplayTag NeedOrMoodTag, float BaseDelta, FGameplayTag ConsumedItemOrActivityTag)
{
    ServerModifyNeedOrMood(NeedOrMoodTag, BaseDelta, ConsumedItemOrActivityTag);
}

void UDynamicSocialComponent::ServerAddOrUpdateProfession(FGameplayTag ProfessionTag, float InitialProficiency, float Priority)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ProfessionTag.IsValid()) return;

    for (FDynamicAssignedProfessionItem& Item : ReplicatedProfessions.Items)
    {
        if (Item.ProfessionTag == ProfessionTag)
        {
            Item.ProficiencyLevel = InitialProficiency;
            Item.ProfessionPriorityWeight = Priority;
            ReplicatedProfessions.MarkItemDirty(Item);
            return;
        }
    }

    FDynamicAssignedProfessionItem NewItem;
    NewItem.ProfessionTag = ProfessionTag;
    NewItem.ProficiencyLevel = InitialProficiency;
    NewItem.ProfessionPriorityWeight = Priority;
    FDynamicAssignedProfessionItem& Added = ReplicatedProfessions.Items.Add_GetRef(NewItem);
    ReplicatedProfessions.MarkItemDirty(Added);
}

bool UDynamicSocialComponent::ServerRemoveProfession(FGameplayTag ProfessionTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ProfessionTag.IsValid()) return false;

    int32 Removed = ReplicatedProfessions.Items.RemoveAll([ProfessionTag](const FDynamicAssignedProfessionItem& Item) {
        return Item.ProfessionTag == ProfessionTag;
    });

    if (Removed > 0)
    {
        ReplicatedProfessions.MarkArrayDirty();
        return true;
    }
    return false;
}

FGameplayTag UDynamicSocialComponent::EvaluateCurrentExpectedActivity(float CurrentGameHour24, FGameplayTag& OutTargetPlaceRoleTag) const
{
    OutTargetPlaceRoleTag = FGameplayTag::EmptyTag;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGameplayTag::EmptyTag;

    for (const FDynamicAssignedProfessionItem& Prof : ReplicatedProfessions.Items)
    {
        if (const FDynamicProfessionDef* Def = GS->ProfessionMasterRegistry.Find(Prof.ProfessionTag))
        {
            for (const FDynamicScheduleBlockDef& Block : Def->DefaultDailySchedule)
            {
                if (CurrentGameHour24 >= Block.StartHour24 && CurrentGameHour24 < Block.EndHour24)
                {
                    OutTargetPlaceRoleTag = Block.PreferredPlaceRoleTag;
                    return Block.ScheduledActivityTag;
                }
            }
        }
    }
    return FGameplayTag::EmptyTag;
}

FString UDynamicSocialComponent::SerializeAgentContextForLLM(ECloudAIRequestType RequestType) const
{
    TSharedRef<FJsonObject> Root = MakeShareable(new FJsonObject());
    Root->SetStringField(TEXT("CharacterPersistentGuid"), CharacterPersistentGuid.ToString());
    Root->SetStringField(TEXT("RequestType"), StaticEnum<ECloudAIRequestType>()->GetNameStringByValue(static_cast<int64>(RequestType)));

    // 1. Personality Traits
    TSharedRef<FJsonObject> PersObj = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> TraitArray;
    for (const FGameplayTag& TraitTag : PersonalityProfile.PersonalityTraitTags)
    {
        TraitArray.Add(MakeShareable(new FJsonValueString(TraitTag.ToString())));
    }
    PersObj->SetArrayField(TEXT("Traits"), TraitArray);
    Root->SetObjectField(TEXT("Personality"), PersObj);

    // 2. Needs
    TArray<TSharedPtr<FJsonValue>> NeedsArray;
    for (const FDynamicNeedStateItem& Need : ReplicatedNeedsAndMoods.Items)
    {
        TSharedRef<FJsonObject> NObj = MakeShareable(new FJsonObject());
        NObj->SetStringField(TEXT("Tag"), Need.NeedOrMoodTag.ToString());
        NObj->SetNumberField(TEXT("Value"), Need.LocalExactValue);
        NeedsArray.Add(MakeShareable(new FJsonValueObject(NObj)));
    }
    Root->SetArrayField(TEXT("Needs"), NeedsArray);

    // 3. Data-Driven Social Graph Metrics (Zero hardcoded properties)
    TArray<TSharedPtr<FJsonValue>> RelArray;
    for (const FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        TSharedRef<FJsonObject> EObj = MakeShareable(new FJsonObject());
        EObj->SetStringField(TEXT("TargetGuid"), Edge.TargetEntityGuid.ToString());
        EObj->SetStringField(TEXT("RelationshipType"), Edge.PrimaryRelationshipTag.ToString());

        TArray<TSharedPtr<FJsonValue>> ContextTagsArr;
        for (const FGameplayTag& CT : Edge.SecondaryContextTags)
        {
            ContextTagsArr.Add(MakeShareable(new FJsonValueString(CT.ToString())));
        }
        EObj->SetArrayField(TEXT("ContextTags"), ContextTagsArr);

        // Export all active metrics dynamically
        TArray<FTagFloatPair> DecayedMetrics = GetAllDecayedRelationshipMetrics(Edge.TargetEntityGuid);
        TSharedRef<FJsonObject> MetricsObj = MakeShareable(new FJsonObject());
        for (const FTagFloatPair& Pair : DecayedMetrics)
        {
            MetricsObj->SetNumberField(Pair.KeyTag.ToString(), Pair.Value);
        }
        EObj->SetObjectField(TEXT("Metrics"), MetricsObj);

        RelArray.Add(MakeShareable(new FJsonValueObject(EObj)));
    }
    Root->SetArrayField(TEXT("Relationships"), RelArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(Root, Writer);
    return OutputString;
}

void UDynamicSocialComponent::ServerSetCloudAISuspension(bool bSuspend)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    if (bIsSuspendedForCloudAI != bSuspend)
    {
        bIsSuspendedForCloudAI = bSuspend;
        OnSuspensionStateChanged.Broadcast(bIsSuspendedForCloudAI);
    }
}

void UDynamicSocialComponent::ServerRequestLLMDecision(ECloudAIRequestType RequestType)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    UWorld* World = GetWorld();
    UDynamicCloudAISubsystem* AISubsystem = World ? World->GetSubsystem<UDynamicCloudAISubsystem>() : nullptr;
    if (!AISubsystem) return;

    ServerSetCloudAISuspension(true);
    const FString ContextJson = SerializeAgentContextForLLM(RequestType);
    ActiveRemoteRequestGuid = AISubsystem->DispatchRemoteAIRequest(GetOwner(), RequestType, ContextJson, 8.0f);
    AISubsystem->OnRemoteAIResponseReceived.AddUniqueDynamic(this, &UDynamicSocialComponent::HandleCloudAIResponseReceived);
}

void UDynamicSocialComponent::HandleCloudAIResponseReceived(const FGuid& RequestGuid, const TArray<FCloudAICommandOutput>& Commands)
{
    if (RequestGuid != ActiveRemoteRequestGuid) return;

    if (UWorld* World = GetWorld())
    {
        if (UDynamicCloudAISubsystem* AISubsystem = World->GetSubsystem<UDynamicCloudAISubsystem>())
        {
            AISubsystem->OnRemoteAIResponseReceived.RemoveDynamic(this, &UDynamicSocialComponent::HandleCloudAIResponseReceived);
        }
    }

    ServerSetCloudAISuspension(false);
    ActiveRemoteRequestGuid.Invalidate();

    if (Commands.Num() == 0) return;

    const FCloudAICommandOutput& BestCmd = Commands[0];
    if (BestCmd.CommandActionTag.IsValid())
    {
        if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (UAbilitySystemComponent* ASC = CharOwner->GetAbilitySystemComponent())
            {
                if (UDynamicGASManagerComponent* GASMgr = CharOwner->FindComponentByClass<UDynamicGASManagerComponent>())
                {
                    GASMgr->TriggerAbilityOrCombo(ASC, BestCmd.CommandActionTag);
                }
            }
        }
    }
}

bool UDynamicSocialComponent::ExportComputeNodePayloadJson(FString& OutTelemetryJson) const
{
    OutTelemetryJson = SerializeAgentContextForLLM(ECloudAIRequestType::ComplexDecision);
    return !OutTelemetryJson.IsEmpty();
}

bool UDynamicSocialComponent::CanInitiateSocialInteraction(FGameplayTag InteractionType, ADynamicCharacter* TargetCharacter) const
{
    if (!TargetCharacter) return false;

    // Guard: Block romantic interactions with minors
    static const FGameplayTag RomanceRoot = FGameplayTag::RequestGameplayTag(TEXT("Social.Interaction.Romantic"), false);
    if (InteractionType.MatchesTag(RomanceRoot))
    {
        if (const UDynamicLifeComponent* TargetLife = TargetCharacter->FindComponentByClass<UDynamicLifeComponent>())
        {
            if (!TargetLife->IsCharacterAdult())
            {
                return false;
            }
        }
        if (const ADynamicCharacter* SelfChar = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (const UDynamicLifeComponent* SelfLife = SelfChar->FindComponentByClass<UDynamicLifeComponent>())
            {
                if (!SelfLife->IsCharacterAdult())
                {
                    return false;
                }
            }
        }
    }
    return true;
}

void UDynamicSocialComponent::EnforceKnownPlaceMemoryCap()
{
    if (ReplicatedKnownPlaces.Items.Num() <= MaxRetainedKnownPlaces)
    {
        return;
    }

    // Sort ascending: lowest confidence, then oldest visit time
    ReplicatedKnownPlaces.Items.Sort([](const FDynamicKnownPlaceItem& A, const FDynamicKnownPlaceItem& B)
    {
        if (!FMath::IsNearlyEqual(A.ConfidenceScore, B.ConfidenceScore, 0.05f))
        {
            return A.ConfidenceScore < B.ConfidenceScore;
        }
        return A.LastVisitedWorldTime < B.LastVisitedWorldTime;
    });

    const int32 NumToPrune = ReplicatedKnownPlaces.Items.Num() - MaxRetainedKnownPlaces;
    for (int32 i = 0; i < NumToPrune; ++i)
    {
        ReplicatedKnownPlaces.Items.RemoveAt(0, 1, EAllowShrinking::No);
    }

    ReplicatedKnownPlaces.MarkArrayDirty();
    RebuildPlaceCache();
}