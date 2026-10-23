// DynamicSocialComponent.cpp
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UDynamicSocialComponent::UDynamicSocialComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    CharacterPersistentGuid = FGuid::NewGuid();

    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedProfessions.OwningSocialComp = this;
}

void UDynamicSocialComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedRelationships.OwningSocialComp = this;
    ReplicatedNeedsAndMoods.OwningSocialComp = this;
    ReplicatedKnownPlaces.OwningSocialComp = this;
    ReplicatedProfessions.OwningSocialComp = this;
}

void UDynamicSocialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_CalloutTimeout);
    }
    Super::EndPlay(EndPlayReason);
}

void UDynamicSocialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UDynamicSocialComponent, CharacterPersistentGuid);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedRelationships);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedNeedsAndMoods);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedKnownPlaces);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedProfessions);
    DOREPLIFETIME(UDynamicSocialComponent, bCloudAISuspended);
}

void FDynamicRelationshipEdgeItem::PostReplicatedAdd(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildRelationshipCache();
}

void FDynamicRelationshipEdgeItem::PostReplicatedChange(const FFastRelationshipEdgeArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildRelationshipCache();
}

void FDynamicNeedStateItem::PreReplicatedRemove(const FFastNeedStateArray& InArrayOwner) {}

void FDynamicNeedStateItem::PostReplicatedAdd(const FFastNeedStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildNeedCache();
}

void FDynamicNeedStateItem::PostReplicatedChange(const FFastNeedStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildNeedCache();
}

void FDynamicKnownPlaceItem::PostReplicatedAdd(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildPlaceCache();
}

void FDynamicKnownPlaceItem::PostReplicatedChange(const FFastKnownPlaceArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildPlaceCache();
}

void FDynamicAssignedProfessionItem::PreReplicatedRemove(const FFastAssignedProfessionArray& InArrayOwner) {}
void FDynamicAssignedProfessionItem::PostReplicatedAdd(const FFastAssignedProfessionArray& InArrayOwner) {}
void FDynamicAssignedProfessionItem::PostReplicatedChange(const FFastAssignedProfessionArray& InArrayOwner) {}

bool UDynamicSocialComponent::GetNeedValue(const FGameplayTag& NeedTag, float& OutValue) const
{
    if (!NeedTag.IsValid()) return false;
    if (const float* FoundVal = CachedNeeds.Find(NeedTag))
    {
        OutValue = *FoundVal;
        return true;
    }
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        if (Item.NeedOrMoodTag.MatchesTagExact(NeedTag))
        {
            OutValue = Item.LocalExactValue;
            return true;
        }
    }
    return false;
}

float UDynamicSocialComponent::GetDecayedRelationshipMetric(const FGuid& TargetEntityGuid, const FGameplayTag& MetricTag) const
{
    if (!TargetEntityGuid.IsValid() || !MetricTag.IsValid()) return 0.0f;
    for (const FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        if (Edge.TargetEntityGuid == TargetEntityGuid)
        {
            return Edge.GetMetricValue(MetricTag);
        }
    }
    return 0.0f;
}

bool UDynamicSocialComponent::GetKnownPlaceByRole(const FGameplayTag& RoleTag, FDynamicKnownPlaceItem& OutPlace) const
{
    if (!RoleTag.IsValid()) return false;
    if (const FDynamicKnownPlaceItem* FoundPlace = CachedPlacesByRole.Find(RoleTag))
    {
        OutPlace = *FoundPlace;
        return true;
    }
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        if (Place.PlaceRoleTag.MatchesTag(RoleTag))
        {
            OutPlace = Place;
            return true;
        }
    }
    return false;
}

void UDynamicSocialComponent::ServerRegisterKnownPlace(const FGameplayTag& PlaceTag, const FGameplayTag& RoleTag, const FGuid& AreaGuid, const FVector& WorldLocation, float InitialConfidence)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    for (FDynamicKnownPlaceItem& Item : ReplicatedKnownPlaces.Items)
    {
        if (Item.PlaceRoleTag.MatchesTagExact(RoleTag) || (AreaGuid.IsValid() && Item.LinkedActorGuid == AreaGuid))
        {
            Item.LastKnownLocation = FVector_NetQuantize10(WorldLocation);
            Item.ConfidenceScore = InitialConfidence;
            Item.LastVisitedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            ReplicatedKnownPlaces.MarkItemDirty(Item);
            RebuildPlaceCache();
            return;
        }
    }

    FDynamicKnownPlaceItem NewPlace;
    NewPlace.PlaceRoleTag = RoleTag;
    NewPlace.PlaceArchetypeOrLandmarkTag = PlaceTag;
    NewPlace.LinkedActorGuid = AreaGuid;
    NewPlace.LastKnownLocation = FVector_NetQuantize10(WorldLocation);
    NewPlace.ConfidenceScore = InitialConfidence;
    NewPlace.LastVisitedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    ReplicatedKnownPlaces.Items.Add(NewPlace);
    ReplicatedKnownPlaces.MarkArrayDirty();
    RebuildPlaceCache();
}

void UDynamicSocialComponent::ServerSetNeedValue(const FGameplayTag& NeedTag, float NewValue)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !NeedTag.IsValid()) return;

    const uint8 Packed = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((NewValue / 100.0f) * 255.0f), 0, 255));
    for (FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        if (Item.NeedOrMoodTag.MatchesTagExact(NeedTag))
        {
            Item.LocalExactValue = NewValue;
            Item.PackedCurrentValue = Packed;
            Item.LastEvaluatedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            ReplicatedNeedsAndMoods.MarkItemDirty(Item);
            CachedNeeds.Add(NeedTag, NewValue);
            return;
        }
    }

    FDynamicNeedStateItem NewItem;
    NewItem.NeedOrMoodTag = NeedTag;
    NewItem.LocalExactValue = NewValue;
    NewItem.PackedCurrentValue = Packed;
    NewItem.LastEvaluatedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    ReplicatedNeedsAndMoods.Items.Add(NewItem);
    ReplicatedNeedsAndMoods.MarkArrayDirty();
    CachedNeeds.Add(NeedTag, NewValue);
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

    const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
    {
        if (Edge.TargetEntityGuid == TargetEntityGuid)
        {
            Edge.CachedTargetActor = TargetActor;
            Edge.LastInteractionWorldTime = CurrentTime;
            Edge.SecondaryContextTags.AppendTags(ContextTags);
            for (const FTagFloatPair& Delta : MetricDeltas)
            {
                const float OldVal = Edge.GetMetricValue(Delta.KeyTag);
                const float NewVal = FMath::Clamp(OldVal + Delta.Value, -100.0f, 100.0f);
                const uint8 PackedVal = static_cast<uint8>(FMath::RoundToInt(((NewVal + 100.0f) / 200.0f) * 255.0f));
                Edge.SetMetricValue(Delta.KeyTag, NewVal, PackedVal);
            }
            ReplicatedRelationships.MarkItemDirty(Edge);
            return;
        }
    }

    FDynamicRelationshipEdgeItem NewEdge;
    NewEdge.TargetEntityGuid = TargetEntityGuid;
    NewEdge.CachedTargetActor = TargetActor;
    NewEdge.TargetKind = TargetKind;
    NewEdge.PrimaryRelationshipTag = RelationshipTypeTag;
    NewEdge.SecondaryContextTags = ContextTags;
    NewEdge.LastInteractionWorldTime = CurrentTime;
    for (const FTagFloatPair& Delta : MetricDeltas)
    {
        const float Val = FMath::Clamp(Delta.Value, -100.0f, 100.0f);
        const uint8 PackedVal = static_cast<uint8>(FMath::RoundToInt(((Val + 100.0f) / 200.0f) * 255.0f));
        NewEdge.SetMetricValue(Delta.KeyTag, Val, PackedVal);
    }
    ReplicatedRelationships.Items.Add(NewEdge);
    ReplicatedRelationships.MarkArrayDirty();
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

    // 1. Filtered Needs (Only serialize urgent or changed needs)
    TSharedPtr<FJsonObject> NeedsObj = MakeShareable(new FJsonObject());
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        const bool bIsUrgent = ContextFilterConfig.bFilterUrgentNeedsOnly && (Item.LocalExactValue < 25.0f || Item.LocalExactValue > 75.0f);
        const bool bHasChanged = ContextFilterConfig.bDeltaOnlySerialization && (Item.LastEvaluatedWorldTime >= LastContextSerializationTime);

        if (!ContextFilterConfig.bFilterUrgentNeedsOnly || bIsUrgent || bHasChanged)
        {
            NeedsObj->SetNumberField(Item.NeedOrMoodTag.ToString(), Item.LocalExactValue);
        }
    }
    RootObj->SetObjectField(TEXT("Needs"), NeedsObj);

    // 2. Spatial & Priority Relationship Filtering
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
                MetricsObj->SetNumberField(Metric.MetricTag.ToString(), Metric.ExactValue);
            }
            RelObj->SetObjectField(TEXT("Metrics"), MetricsObj);

            RelationshipsArray.Add(MakeShareable(new FJsonValueObject(RelObj)));
            PackedRelationships++;
        }
    }
    RootObj->SetArrayField(TEXT("NearbyRelationships"), RelationshipsArray);

    // 3. Known Places
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

bool UDynamicSocialComponent::TryResolveFastPathDialogue(
    FGameplayTag CategoryTag,
    AActor* TargetActor,
    FString& OutUtterance,
    TSoftObjectPtr<UAnimMontage>& OutMontage,
    FGameplayTag& OutVoiceCueTag)
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    // 1. Collect NPC's current emotional mood tags
    FGameplayTagContainer CurrentMoodTags;
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        CurrentMoodTags.AddTag(Item.NeedOrMoodTag);
    }

    // 2. Resolve relationship with target actor (if applicable)
    const FDynamicRelationshipEdgeItem* TargetEdge = nullptr;
    if (TargetActor)
    {
        if (const UDynamicSocialComponent* TargetSocial = TargetActor->FindComponentByClass<UDynamicSocialComponent>())
        {
            for (const FDynamicRelationshipEdgeItem& Edge : ReplicatedRelationships.Items)
            {
                if (Edge.TargetEntityGuid == TargetSocial->CharacterPersistentGuid)
                {
                    TargetEdge = &Edge;
                    break;
                }
            }
        }
    }

    // 3. Search and filter dialogue definitions matching category, moods, and relational attributes
    TArray<const FFastPathDialogueEntry*> ValidCandidates;
    float TotalWeight = 0.0f;

    for (const auto& RegistryPair : GS->FastPathDialogueRegistry)
    {
        for (const FFastPathDialogueEntry& Entry : RegistryPair.Value.Entries)
        {
            if (!Entry.CategoryTag.MatchesTag(CategoryTag)) continue;

            // Check emotional moods: if entry has required moods, NPC must possess at least one
            if (Entry.AllowedMoodTags.Num() > 0 && !CurrentMoodTags.HasAny(Entry.AllowedMoodTags))
            {
                continue;
            }

            // Check relationship type requirements
            if (Entry.AllowedRelationshipTypeTags.Num() > 0)
            {
                if (!TargetEdge || !Entry.AllowedRelationshipTypeTags.HasTag(TargetEdge->PrimaryRelationshipTag))
                {
                    continue;
                }
            }

            // Check multi-attribute relational requirements (Affinity, Hostility, Trust, etc.)
            bool bAttributesMatch = true;
            for (const FCharacterAttributeRangeDef& Req : Entry.RelationalAttributeRequirements)
            {
                const float CurrentVal = TargetEdge ? TargetEdge->GetMetricValue(Req.AttributeTag) : 0.0f;
                if (CurrentVal < Req.MinValue || CurrentVal > Req.MaxValue)
                {
                    bAttributesMatch = false;
                    break;
                }
            }
            if (!bAttributesMatch) continue;

            ValidCandidates.Add(&Entry);
            TotalWeight += FMath::Max(0.01f, Entry.SelectionWeight);
        }
    }

    if (ValidCandidates.Num() == 0) return false;

    // 4. Weighted random selection
    const float Roll = FMath::FRandRange(0.0f, TotalWeight);
    float Cumulative = 0.0f;
    const FFastPathDialogueEntry* ChosenEntry = ValidCandidates[0];

    for (const FFastPathDialogueEntry* Candidate : ValidCandidates)
    {
        Cumulative += FMath::Max(0.01f, Candidate->SelectionWeight);
        if (Roll <= Cumulative)
        {
            ChosenEntry = Candidate;
            break;
        }
    }

    // 5. Contextual Token Replacement
    OutUtterance = FormatDialogueTokens(ChosenEntry->UtteranceTemplate, TargetActor);
    //OutMontage = ChosenEntry->GestureMontage;
    OutVoiceCueTag = ChosenEntry->VoiceCueTag;

    OnFastPathDialogueSpoken.Broadcast(GetOwner(), TargetActor, OutUtterance);
    return true;
}

bool UDynamicSocialComponent::TriggerCalloutToTarget(AActor* TargetActor, float IgnoreTimeout)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetActor) return false;

    FString Utterance;
    TSoftObjectPtr<UAnimMontage> Montage;
    FGameplayTag VoiceCue;

    static const FGameplayTag CalloutTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Category.Callout"), false);
    if (TryResolveFastPathDialogue(CalloutTag, TargetActor, Utterance, Montage, VoiceCue))
    {
        ActiveCalloutTarget = TargetActor;

        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(
                TimerHandle_CalloutTimeout,
                this,
                &UDynamicSocialComponent::HandleCalloutIgnored,
                IgnoreTimeout,
                false
            );
        }
        return true;
    }
    return false;
}

void UDynamicSocialComponent::HandleCalloutIgnored()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    AActor* Target = ActiveCalloutTarget.Get();
    if (Target)
    {
        // 1. Penalize relationship for ignoring callout
        if (const UDynamicSocialComponent* TargetSocial = Target->FindComponentByClass<UDynamicSocialComponent>())
        {
            TArray<FTagFloatPair> Deltas;
            FTagFloatPair AffDelta;
            AffDelta.KeyTag = FGameplayTag::RequestGameplayTag(TEXT("Relationship.Metric.Affinity"), false);
            AffDelta.Value = -10.0f;
            Deltas.Add(AffDelta);

            FTagFloatPair HostilityDelta;
            HostilityDelta.KeyTag = FGameplayTag::RequestGameplayTag(TEXT("Relationship.Metric.Hostility"), false);
            HostilityDelta.Value = 15.0f;
            Deltas.Add(HostilityDelta);

            ServerRecordSocialInteraction(
                TargetSocial->CharacterPersistentGuid,
                Target,
                ERelationshipTargetKind::Character,
                FGameplayTag::RequestGameplayTag(TEXT("Relationship.Type.Rivalry"), false),
                Deltas,
                FGameplayTagContainer()
            );
        }

        // 2. Play irritated reaction utterance
        FString OutUtterance;
        TSoftObjectPtr<UAnimMontage> OutMontage;
        FGameplayTag OutVoiceCue;
        static const FGameplayTag IgnoredTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Category.Callout.Ignored"), false);
        TryResolveFastPathDialogue(IgnoredTag, Target, OutUtterance, OutMontage, OutVoiceCue);
    }

    ClearActiveCallout();
}

void UDynamicSocialComponent::ClearActiveCallout()
{
    ActiveCalloutTarget.Reset();
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TimerHandle_CalloutTimeout);
    }
}

FString UDynamicSocialComponent::FormatDialogueTokens(
    const FString& TemplateString,
    AActor* TargetActor,
    const FString& CustomLocation,
    const FString& CustomTime) const
{
    FString Formatted = TemplateString;

    // %speaker% replacement
    const FString SpeakerName = GetOwner() ? GetOwner()->GetName() : TEXT("Stranger");
    Formatted = Formatted.Replace(TEXT("%speaker%"), *SpeakerName);

    // %target% replacement
    const FString TargetName = TargetActor ? TargetActor->GetName() : TEXT("traveler");
    Formatted = Formatted.Replace(TEXT("%target%"), *TargetName);

    // Contextual world tokens
    Formatted = Formatted.Replace(TEXT("%location_type%"), *CustomLocation);
    Formatted = Formatted.Replace(TEXT("%recent_time%"), *CustomTime);

    return Formatted;
}

void UDynamicSocialComponent::RebuildRelationshipCache() {}

void UDynamicSocialComponent::RebuildNeedCache()
{
    CachedNeeds.Reset();
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        CachedNeeds.Add(Item.NeedOrMoodTag, Item.LocalExactValue);
    }
}

void UDynamicSocialComponent::RebuildPlaceCache()
{
    CachedPlacesByRole.Reset();
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        if (Place.PlaceRoleTag.IsValid())
        {
            CachedPlacesByRole.Add(Place.PlaceRoleTag, Place);
        }
    }
}

// ============================================================================
// FAST ARRAY CALLBACK IMPLEMENTATIONS FOR LINKER (LNK2019 FIX)
// ============================================================================

void FDynamicRelationshipItem::PreReplicatedRemove(const FDynamicRelationshipFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->RebuildRelationshipCache();
    }
}

void FDynamicRelationshipItem::PostReplicatedAdd(const FDynamicRelationshipFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->RebuildRelationshipCache();
    }
}

void FDynamicRelationshipItem::PostReplicatedChange(const FDynamicRelationshipFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->RebuildRelationshipCache();
    }
}

void FDynamicNeedAndMoodItem::PreReplicatedRemove(const FDynamicNeedAndMoodFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->RebuildNeedCache();
    }
}

void FDynamicNeedAndMoodItem::PostReplicatedAdd(const FDynamicNeedAndMoodFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->RebuildNeedCache();
    }
}

void FDynamicNeedAndMoodItem::PostReplicatedChange(const FDynamicNeedAndMoodFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp)
    {
        InArrayOwner.OwningSocialComp->RebuildNeedCache();
    }
}

bool UDynamicSocialComponent::SelectBestUtterance(const TArray<FDynamicDialogueUtteranceDef>& CandidateUtterances, FDynamicDialogueUtteranceDef& OutSelectedUtterance) const
{
    if (CandidateUtterances.Num() == 0) return false;

    AActor* OwnerActor = GetOwner();
    const UAbilitySystemComponent* ASC = OwnerActor ? OwnerActor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
    const UDynamicAttributeSet* AttrSet = ASC ? Cast<UDynamicAttributeSet>(ASC->GetAttributeSet(UDynamicAttributeSet::StaticClass())) : nullptr;

    struct FEligibleUtterance
    {
        const FDynamicDialogueUtteranceDef* Def = nullptr;
        float ComputedWeight = 0.0f;
    };

    TArray<FEligibleUtterance> ValidOptions;
    float TotalWeight = 0.0f;

    for (const FDynamicDialogueUtteranceDef& Option : CandidateUtterances)
    {
        bool bPassesRequirements = true;

        // 1. Evaluate Absolute Attribute Range Requirements
        for (const FCharacterAttributeRangeDef& Req : Option.AttributeRequirements)
        {
            if (!Req.AttributeTag.IsValid()) continue;

            float Val = 0.0f;
            if (AttrSet)
            {
                Val = AttrSet->GetDynamicAttributeValue(Req.AttributeTag, 0);
            }
            if (FMath::IsNearlyZero(Val))
            {
                GetNeedValue(Req.AttributeTag, Val);
            }

            if (Val < Req.MinValue || Val > Req.MaxValue)
            {
                bPassesRequirements = false;
                break;
            }
        }

        if (!bPassesRequirements) continue;

        // 2. Compute Dynamic Probability Weight
        float FinalWeight = Option.SelectionWeight;
        for (const auto& ScalarPair : Option.ProbabilityAttributeScalars.LocalLookupMap)
        {
            if (!ScalarPair.Key.IsValid()) continue;

            float AttrVal = 0.0f;
            if (AttrSet)
            {
                AttrVal = AttrSet->GetDynamicAttributeValue(ScalarPair.Key, 0);
            }
            if (FMath::IsNearlyZero(AttrVal))
            {
                GetNeedValue(ScalarPair.Key, AttrVal);
            }

            FinalWeight += (AttrVal * ScalarPair.Value);
        }

        FinalWeight = FMath::Max(0.01f, FinalWeight);
        TotalWeight += FinalWeight;

        FEligibleUtterance Item;
        Item.Def = &Option;
        Item.ComputedWeight = FinalWeight;
        ValidOptions.Add(Item);
    }

    if (ValidOptions.Num() == 0) return false;

    // 3. Weighted Lottery Selection
    const float Roll = FMath::FRandRange(0.0f, TotalWeight);
    float RunningSum = 0.0f;

    for (const FEligibleUtterance& Item : ValidOptions)
    {
        RunningSum += Item.ComputedWeight;
        if (Roll <= RunningSum)
        {
            OutSelectedUtterance = *Item.Def;
            return true;
        }
    }

    OutSelectedUtterance = *ValidOptions.Last().Def;
    return true;
}