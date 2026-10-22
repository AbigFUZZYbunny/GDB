// DynamicSocialComponent.cpp
#include "DynamicSocialComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
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

// FastArray Item Replication callbacks
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

    // Serialize current needs
    TSharedPtr<FJsonObject> NeedsObj = MakeShareable(new FJsonObject());
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        NeedsObj->SetNumberField(Item.NeedOrMoodTag.ToString(), Item.LocalExactValue);
    }
    RootObj->SetObjectField(TEXT("Needs"), NeedsObj);

    // Serialize known places
    TArray<TSharedPtr<FJsonValue>> PlacesArray;
    for (const FDynamicKnownPlaceItem& Place : ReplicatedKnownPlaces.Items)
    {
        TSharedPtr<FJsonObject> PlaceObj = MakeShareable(new FJsonObject());
        PlaceObj->SetStringField(TEXT("Role"), Place.PlaceRoleTag.ToString());
        PlaceObj->SetStringField(TEXT("Landmark"), Place.PlaceArchetypeOrLandmarkTag.ToString());
        PlacesArray.Add(MakeShareable(new FJsonValueObject(PlaceObj)));
    }
    RootObj->SetArrayField(TEXT("KnownPlaces"), PlacesArray);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObj, Writer);
    return OutputString;
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

// --- FastArray Callback Implementations for Relationships & Needs ---

void FDynamicRelationshipItem::PreReplicatedRemove(const FDynamicRelationshipFastArray& InArrayOwner) {}

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


void FDynamicNeedAndMoodItem::PreReplicatedRemove(const FDynamicNeedAndMoodFastArray& InArrayOwner) {}

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