// DynamicSocialComponent.cpp
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

UDynamicSocialComponent::UDynamicSocialComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UDynamicSocialComponent::BeginPlay()
{
    Super::BeginPlay();
    RebuildRelationshipCache();
    RebuildNeedCache();
    RebuildPlaceCache();
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

void UDynamicSocialComponent::RebuildRelationshipCache()
{
    // Local fast-lookup relationship mapping
}

void UDynamicSocialComponent::RebuildNeedCache()
{
    CachedNeeds.Empty(ReplicatedNeedsAndMoods.Items.Num());
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        CachedNeeds.Add(Item.NeedOrMoodTag, Item.LocalExactValue);
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
        if (Item.NeedOrMoodTag == NeedTag)
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
    if (const FDynamicRelationshipEdgeItem* Edge = FindRelationshipByGuid(TargetEntityGuid))
    {
        return Edge->GetMetricValue(MetricTag);
    }
    return 0.0f;
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

    const uint8 Packed = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((NewValue / 100.0f) * 255.0f), 0, 255));
    for (FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        if (Item.NeedOrMoodTag == NeedTag)
        {
            Item.LocalExactValue = FMath::Clamp(NewValue, 0.0f, 100.0f);
            Item.PackedCurrentValue = Packed;
            Item.LastEvaluatedWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            ReplicatedNeedsAndMoods.MarkItemDirty(Item);
            RebuildNeedCache();
            return;
        }
    }

    FDynamicNeedStateItem NewItem;
    NewItem.NeedOrMoodTag = NeedTag;
    NewItem.LocalExactValue = FMath::Clamp(NewValue, 0.0f, 100.0f);
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
        NewEdge.PrimaryRelationshipTag = RelationshipTypeTag.IsValid() ? RelationshipTypeTag : FGameplayTag::RequestGameplayTag(TEXT("Relation.Type.Acquaintance"), false);
        NewEdge.SecondaryContextTags = ContextTags;
        NewEdge.LastInteractionWorldTime = CurrentTime;
        
        FDynamicRelationshipEdgeItem& Added = ReplicatedRelationships.Items.Add_GetRef(NewEdge);
        ReplicatedRelationships.MarkItemDirty(Added);
        FoundEdge = &Added;
    }

    FoundEdge->LastInteractionWorldTime = CurrentTime;
    FoundEdge->CachedTargetActor = TargetActor;
    FoundEdge->SecondaryContextTags.AppendTags(ContextTags);

    for (const FTagFloatPair& Delta : MetricDeltas)
    {
        if (!Delta.KeyTag.IsValid()) continue;
        const float CurVal = FoundEdge->GetMetricValue(Delta.KeyTag);
        const float NewVal = FMath::Clamp(CurVal + Delta.Value, -100.0f, 100.0f);
        const uint8 PackedVal = static_cast<uint8>(FMath::RoundToInt(((NewVal + 100.0f) / 200.0f) * 255.0f));
        FoundEdge->SetMetricValue(Delta.KeyTag, NewVal, PackedVal);
    }

    ReplicatedRelationships.MarkItemDirty(*FoundEdge);
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
            NeedsObj->SetNumberField(Item.NeedOrMoodTag.ToString(), Item.LocalExactValue);
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
                MetricsObj->SetNumberField(Metric.MetricTag.ToString(), Metric.ExactValue);
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
        SpeakerMoodTags.AddTag(MoodItem.NeedOrMoodTag);
    }

    FGameplayTagContainer TargetMoodTags;
    if (TargetSocial)
    {
        for (const FDynamicNeedStateItem& MoodItem : TargetSocial->ReplicatedNeedsAndMoods.Items)
        {
            TargetMoodTags.AddTag(MoodItem.NeedOrMoodTag);
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
            const float MetricVal = RelationEdge ? RelationEdge->GetMetricValue(RelReq.AttributeTag) : 0.0f;
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
                const float MetricVal = RelationEdge->GetMetricValue(RelScalar.Key);
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
        CurrentMoodTags.AddTag(Item.NeedOrMoodTag);
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
            const float CurrentVal = TargetEdge ? TargetEdge->GetMetricValue(Req.AttributeTag) : 0.0f;
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

// --- Fast Array Lifecycle Implementations (Resolving Linker Unresolved Externals) ---
//void FDynamicRelationshipEdgeItem::PreReplicatedRemove(const FFastRelationshipEdgeArray& InArrayOwner) {}
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

//void FDynamicKnownPlaceItem::PreReplicatedRemove(const FFastKnownPlaceArray& InArrayOwner) {}
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

void FDynamicRelationshipItem::PreReplicatedRemove(const FDynamicRelationshipFastArray& InArrayOwner) {}
void FDynamicRelationshipItem::PostReplicatedAdd(const FDynamicRelationshipFastArray& InArrayOwner) {}
void FDynamicRelationshipItem::PostReplicatedChange(const FDynamicRelationshipFastArray& InArrayOwner) {}

void FDynamicNeedAndMoodItem::PreReplicatedRemove(const FDynamicNeedAndMoodFastArray& InArrayOwner) {}
void FDynamicNeedAndMoodItem::PostReplicatedAdd(const FDynamicNeedAndMoodFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildNeedCache();
}
void FDynamicNeedAndMoodItem::PostReplicatedChange(const FDynamicNeedAndMoodFastArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildNeedCache();
}