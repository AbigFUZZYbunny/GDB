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
#include "DynamicCloudAISubsystem.h"
#include "DynamicStateTreeComponent.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Net/UnrealNetwork.h"
#include "Engine/OverlapResult.h"

// Helper function in DynamicSocialComponent.cpp to register language exposure with nearby listeners
static void BroadcastLanguageExposureToNearbyListeners(AActor* Speaker, FGameplayTag LanguageTag, const FString& Utterance)
{
    if (!Speaker || !Speaker->HasAuthority() || !LanguageTag.IsValid() || Utterance.IsEmpty()) return;

    UWorld* World = Speaker->GetWorld();
    if (!World) return;

    // 1500cm (15m) hearing range for learning spoken vocabulary
    constexpr float HearingRadius = 1500.0f;
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(HearingRadius);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(LanguageExposureSweep), false, Speaker);

    if (World->OverlapMultiByChannel(Overlaps, Speaker->GetActorLocation(), FQuat::Identity, ECC_Pawn, Sphere, Params))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            if (AActor* Listener = Overlap.GetActor())
            {
                if (Listener != Speaker)
                {
                    if (UDynamicLanguageComponent* LangComp = Listener->FindComponentByClass<UDynamicLanguageComponent>())
                    {
                        LangComp->ServerRegisterLanguageExposure(LanguageTag, Utterance);
                    }
                }
            }
        }
    }
}

static void BroadcastLanguageExposureAcoustic(
    AActor* Speaker, 
    FGameplayTag LanguageTag, 
    const FString& Utterance, 
    float BaseAudibleRadius, 
    FGameplayTag VolumeTag,
    FGameplayTag EmotionTag = FGameplayTag(),
    FGameplayTag VoiceProfileTag = FGameplayTag())
{
    if (!Speaker || !Speaker->HasAuthority() || !LanguageTag.IsValid() || Utterance.IsEmpty()) return;

    UWorld* World = Speaker->GetWorld();
    if (!World) return;

    if (!VoiceProfileTag.IsValid())
    {
        if (const UDynamicLanguageComponent* SpeakerLang = Speaker->FindComponentByClass<UDynamicLanguageComponent>())
        {
            VoiceProfileTag = SpeakerLang->ResolveActiveVoiceProfile();
        }
    }

    const float MaxQueryRadius = FMath::Clamp(BaseAudibleRadius * 4.5f, 300.0f, 15000.0f);
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(MaxQueryRadius);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(LanguageAcousticSweep), false, Speaker);

    const FVector SpeakerLoc = Speaker->GetActorLocation();

    if (World->OverlapMultiByChannel(Overlaps, SpeakerLoc, FQuat::Identity, ECC_Pawn, Sphere, Params))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            AActor* Listener = Overlap.GetActor();
            if (Listener && Listener != Speaker)
            {
                if (UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    const float DistUnits = FVector::Dist(SpeakerLoc, Listener->GetActorLocation());

                    // Evaluates perception attributes, buffs, and wall occlusion
                    if (ListenerLang->CanHearUtteranceAtDistance(DistUnits, BaseAudibleRadius, VolumeTag, SpeakerLoc))
                    {
                        // A. Linguistic Exposure
                        ListenerLang->ServerRegisterLanguageExposure(LanguageTag, Utterance);

                        // B. Client Subtitle RPC for Player Listeners (7 arguments passed)
                        APawn* ListenerPawn = Cast<APawn>(Listener);
                        if (ListenerPawn && ListenerPawn->IsPlayerControlled())
                        {
                            ListenerLang->ClientReceiveHeardDialogue(
                                Speaker, 
                                Utterance, 
                                LanguageTag, 
                                BaseAudibleRadius, 
                                VolumeTag, 
                                EmotionTag, 
                                VoiceProfileTag
                            );
                        }

                        // C. AI Agent Ingestion & Conversational Reaction
                        if (UDynamicSocialComponent* ListenerSocial = Listener->FindComponentByClass<UDynamicSocialComponent>())
                        {
                            ListenerSocial->RecordDialogueTurn(
                                Speaker->FindComponentByClass<UDynamicSocialComponent>() ? Speaker->FindComponentByClass<UDynamicSocialComponent>()->CharacterPersistentGuid : FGuid(),
                                Speaker->FindComponentByClass<UDynamicSocialComponent>() ? Speaker->FindComponentByClass<UDynamicSocialComponent>()->CharacterDisplayName : Speaker->GetName(),
                                LanguageTag,
                                Utterance
                            );

                            if (!ListenerPawn || !ListenerPawn->IsPlayerControlled())
                            {
                                ListenerSocial->HandleHeardDialogueFromSpeaker(Speaker, Utterance, LanguageTag, VolumeTag);
                            }
                        }
                    }
                }
            }
        }
    }
}

void UDynamicSocialComponent::CleanExpiredDialogueTurns()
{
    const UWorld* World = GetWorld();
    if (!World || ConversationalMemoryTimeoutSeconds <= 0.0f) return;

    const double CurrentTime = World->GetTimeSeconds();
    TMap<FGuid, TArray<FDynamicDialogueTurn>> ExpiredTurnsByTarget;

    for (int32 i = RecentDialogueHistory.Num() - 1; i >= 0; --i)
    {
        if ((CurrentTime - RecentDialogueHistory[i].WorldTimestamp) > ConversationalMemoryTimeoutSeconds)
        {
            FGuid RelevantGuid = RecentDialogueHistory[i].SpeakerGuid;
            
            // Group expired turns by the entity we were speaking with
            if (RelevantGuid.IsValid() && RelevantGuid != CharacterPersistentGuid)
            {
                // Insert at 0 to maintain chronological order from the bottom-up iteration
                ExpiredTurnsByTarget.FindOrAdd(RelevantGuid).Insert(RecentDialogueHistory[i], 0);
            }
            
            RecentDialogueHistory.RemoveAt(i);
        }
    }

    // Trigger asynchronous memory compression for any actors whose conversations just timed out
    for (const auto& Pair : ExpiredTurnsByTarget)
    {
        if (Pair.Value.Num() > 0)
        {
            TriggerMemoryConsolidation(Pair.Key, Pair.Value);
        }
    }
}

void UDynamicSocialComponent::RecordNonVerbalAction(const FGuid& ActorGuid, const FString& ActorName, const FString& ActionDescription)
{
    CleanExpiredDialogueTurns();

    FDynamicDialogueTurn Turn;
    Turn.SpeakerGuid = ActorGuid;
    Turn.SpeakerName = ActorName;
    
    // Explicitly flag as an action rather than a spoken language
    Turn.SpokenLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Action"), false); 
    Turn.UtteranceText = FString::Printf(TEXT("*%s*"), *ActionDescription);
    Turn.WorldTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    RecentDialogueHistory.Add(Turn);

    while (RecentDialogueHistory.Num() > MaxDialogueHistoryTurns)
    {
        // If we overflow the buffer during active conversation, immediately consolidate the oldest turn
        FGuid DroppedGuid = RecentDialogueHistory[0].SpeakerGuid;
        if (DroppedGuid.IsValid() && DroppedGuid != CharacterPersistentGuid)
        {
            TArray<FDynamicDialogueTurn> OverflowArray;
            OverflowArray.Add(RecentDialogueHistory[0]);
            TriggerMemoryConsolidation(DroppedGuid, OverflowArray);
        }
        RecentDialogueHistory.RemoveAt(0);
    }
}

void UDynamicSocialComponent::RecordDialogueTurn(const FGuid& SpeakerGuid, const FString& SpeakerName, FGameplayTag SpokenLanguageTag, const FString& UtteranceText)
{
    CleanExpiredDialogueTurns();

    FDynamicDialogueTurn Turn;
    Turn.SpeakerGuid = SpeakerGuid;
    Turn.SpeakerName = SpeakerName;
    Turn.SpokenLanguageTag = SpokenLanguageTag;
    Turn.UtteranceText = UtteranceText;
    Turn.WorldTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    RecentDialogueHistory.Add(Turn);

    // Bounded history buffer management
    while (RecentDialogueHistory.Num() > MaxDialogueHistoryTurns)
    {
        FGuid DroppedGuid = RecentDialogueHistory[0].SpeakerGuid;
        if (DroppedGuid.IsValid() && DroppedGuid != CharacterPersistentGuid)
        {
            TArray<FDynamicDialogueTurn> OverflowArray;
            OverflowArray.Add(RecentDialogueHistory[0]);
            TriggerMemoryConsolidation(DroppedGuid, OverflowArray);
        }
        RecentDialogueHistory.RemoveAt(0);
    }
}

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
    DOREPLIFETIME(UDynamicSocialComponent, ActiveTurnState);
    DOREPLIFETIME(UDynamicSocialComponent, ActiveInterlocutor);
    DOREPLIFETIME(UDynamicSocialComponent, ReplicatedTopics);
    DOREPLIFETIME(UDynamicSocialComponent, ActiveConversationalCircleGuid);
}

void UDynamicSocialComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (ActiveTurnState != EConversationalTurnState::Idle)
    {
        UpdateConversationalGaze(DeltaTime);
    }
}

void UDynamicSocialComponent::OnRep_Relationships() { RebuildRelationshipCache(); }
void UDynamicSocialComponent::OnRep_NeedsAndMoods() { RebuildNeedCache(); }
void UDynamicSocialComponent::OnRep_KnownPlaces() { RebuildPlaceCache(); }
void UDynamicSocialComponent::OnRep_Professions() {}
void UDynamicSocialComponent::OnRep_CrimeMemories() {}
void UDynamicSocialComponent::HandleSingleCrimeMemoryReplicated(const FCrimeMemoryItem& Item) {}

void UDynamicSocialComponent::UpdateConversationalGaze(float DeltaSeconds)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor) return;

    AActor* TargetActor = ActiveInterlocutor.Get();

    // In a multi-party circle, alternate gaze periodically to simulate natural eye contact
    if (CircleParticipants.Num() > 1)
    {
        GazeSwitchTimer += DeltaSeconds;
        if (GazeSwitchTimer >= 4.5f)
        {
            GazeSwitchTimer = 0.0f;
            const int32 NextIdx = FMath::RandRange(0, CircleParticipants.Num() - 1);
            if (CircleParticipants[NextIdx].ParticipantActor.IsValid())
            {
                TargetActor = CircleParticipants[NextIdx].ParticipantActor.Get();
            }
        }
    }

    if (!TargetActor) return;

    const FVector HeadTargetLocation = TargetActor->GetActorLocation() + FVector(0.0f, 0.0f, 65.0f);
    CurrentGazeLocation = FMath::VInterpTo(CurrentGazeLocation, HeadTargetLocation, DeltaSeconds, 4.0f);

    OnConversationalGazeUpdated.Broadcast(CurrentGazeLocation);

    // Physical look-at drive via PhysicsControlComponent
    ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(OwnerActor);
    if (!DynChar) return;

    UPhysicsControlComponent* PCC = DynChar->GetPhysicsControlComponent();
    if (!PCC) return;

    const FVector LookVector = (CurrentGazeLocation - (OwnerActor->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f))).GetSafeNormal();
    const FRotator TargetLookRot = LookVector.Rotation();

    // Smoothly orient head control toward the speaker
    static const FName HeadControlName = TEXT("Control_head");
    PCC->SetControlTargetOrientation(HeadControlName, TargetLookRot, 35.0f);
}

void UDynamicSocialComponent::ServerJoinConversationalCircle(const FGuid& CircleGuid, AActor* ParticipantActor)
{
    if (!GetOwner()->HasAuthority() || !CircleGuid.IsValid() || !ParticipantActor) return;

    ActiveConversationalCircleGuid = CircleGuid;

    // Check if participant already recorded
    for (FConversationalCircleParticipant& P : CircleParticipants)
    {
        if (P.ParticipantActor.Get() == ParticipantActor)
        {
            P.LastSpokenTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
            P.AttentionWeight = 1.0f;
            return;
        }
    }

    FConversationalCircleParticipant NewP;
    NewP.ParticipantActor = ParticipantActor;
    if (UDynamicSocialComponent* PSocial = ParticipantActor->FindComponentByClass<UDynamicSocialComponent>())
    {
        NewP.CharacterGuid = PSocial->CharacterPersistentGuid;
    }
    NewP.LastSpokenTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    NewP.AttentionWeight = 1.0f;
    CircleParticipants.Add(NewP);

    // If an NPC joined, face the newest participant
    ActiveInterlocutor = ParticipantActor;
}

void UDynamicSocialComponent::ServerLeaveConversationalCircle(AActor* ParticipantActor)
{
    if (!GetOwner()->HasAuthority() || !ParticipantActor) return;

    CircleParticipants.RemoveAll([ParticipantActor](const FConversationalCircleParticipant& P) {
        return P.ParticipantActor.Get() == ParticipantActor;
    });

    if (CircleParticipants.Num() == 0)
    {
        ActiveConversationalCircleGuid.Invalidate();
        ServerReleaseConversationalLock();
    }
    else if (ActiveInterlocutor.Get() == ParticipantActor)
    {
        ActiveInterlocutor = CircleParticipants[0].ParticipantActor.Get();
    }
}

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

    // 1. Identity, Race, and Native Language Directive    
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        RootObj->SetStringField(TEXT("AgentDisplayName"), ResolveActorDisplayName(CharOwner));
        
        if (const UDynamicCustomizationComponent* CustComp = CharOwner->GetCustomizationComponent())
        {
            RootObj->SetStringField(TEXT("RaceTemplate"), CustComp->ActiveCharacterTemplateTag.ToString());
        }

        if (const UDynamicLanguageComponent* LangComp = CharOwner->FindComponentByClass<UDynamicLanguageComponent>())
        {
            RootObj->SetStringField(TEXT("NativeLanguage"), LangComp->ActiveSpokenLanguageTag.ToString());
            RootObj->SetNumberField(TEXT("CommonFluency"), LangComp->GetLanguageFluency(FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false)));
            RootObj->SetStringField(TEXT("AssignedVoiceProfile"), LangComp->ResolveActiveVoiceProfile().ToString());
        }
    }

    // 2. Guide the LLM to output expressive punctuation suited for Speech Synthesis
    RootObj->SetStringField(TEXT("SystemInstruction"), 
        TEXT("You are an NPC AI brain. You MUST reply ONLY with a raw JSON object matching: ")
        TEXT("{\"Commands\":[{\"CommandActionTag\":\"Action.Interact.Chat\",\"TargetEntityOrPlaceRoleTag\":\"Role.Place.TownSquare\",\"UtteranceText\":\"<English sentence>\",\"PriorityWeight\":100.0, \"EmotionTag\":\"Emotion.Happy\"}]}. ")
        TEXT("Write UtteranceText with clear punctuation (commas, periods, exclamation points) to guide natural Text-to-Speech pauses and prosody. ")
        TEXT("Set EmotionTag to reflect your internal state (Emotion.Happy, Emotion.Angry, Emotion.Sad, Emotion.Fear, Emotion.Pensive, Emotion.Neutral). ")
        TEXT("Do NOT wrap in markdown backticks.")
    );

    // 3. Multi-Turn Conversational Dialogue History (Short-term conversation buffer)
    if (RecentDialogueHistory.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> HistoryArray;
        for (const FDynamicDialogueTurn& Turn : RecentDialogueHistory)
        {
            TSharedPtr<FJsonObject> TurnObj = MakeShareable(new FJsonObject());
            TurnObj->SetStringField(TEXT("Speaker"), Turn.SpeakerName);
            TurnObj->SetStringField(TEXT("Language"), Turn.SpokenLanguageTag.ToString());
            TurnObj->SetStringField(TEXT("Text"), Turn.UtteranceText);
            HistoryArray.Add(MakeShareable(new FJsonValueObject(TurnObj)));
        }
        RootObj->SetArrayField(TEXT("ConversationHistory"), HistoryArray);
    }

    const double CurrentWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const AActor* OwnerActor = GetOwner();
    const FVector OwnerLoc = OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;

    // 4. Needs & Moods
    TSharedPtr<FJsonObject> NeedsObj = MakeShareable(new FJsonObject());
    for (const FDynamicNeedStateItem& Item : ReplicatedNeedsAndMoods.Items)
    {
        const bool bIsUrgent = ContextFilterConfig.bFilterUrgentNeedsOnly && (Item.LocalExactValue < 25.0f || Item.LocalExactValue > 75.0f);
        const bool bHasChanged = ContextFilterConfig.bDeltaOnlySerialization && (Item.LastEvaluatedWorldTime >= LastContextSerializationTime);

        if (!ContextFilterConfig.bFilterUrgentNeedsOnly || bIsUrgent || bHasChanged)
        {
            NeedsObj->SetNumberField(Item.NeedTag.ToString(), Item.LocalExactValue);
        }
    }
    RootObj->SetObjectField(TEXT("Needs"), NeedsObj);

    // 5. Spatial Relationships & Long-Term Memory Summaries
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
            
            // NEW: Inject Long-Term Memory Summary for this specific character
            if (!Edge.LongTermMemorySummary.IsEmpty())
            {
                RelObj->SetStringField(TEXT("MemorySummary"), Edge.LongTermMemorySummary);
            }

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

    // 6. Known Places
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

    // 7. Active Quests
    if (const UDynamicQuestComponent* QuestComp = OwnerActor->FindComponentByClass<UDynamicQuestComponent>())
    {
        TArray<TSharedPtr<FJsonValue>> QuestsArray;
        for (const FDynamicActiveQuestItem& Quest : QuestComp->GetActiveQuests().Items)
        {
            TSharedPtr<FJsonObject> QObj = MakeShareable(new FJsonObject());
            QObj->SetStringField(TEXT("QuestTag"), Quest.QuestDefTag.ToString());
            QObj->SetStringField(TEXT("Status"), UEnum::GetValueAsString(Quest.StepState));
            
            TArray<TSharedPtr<FJsonValue>> ObjArray;
            for (const FDynamicActiveQuestObjectiveItem& Obj : QuestComp->GetActiveObjectives().Items)
            {
                if (Obj.QuestInstanceGuid == Quest.QuestInstanceGuid)
                {
                    TSharedPtr<FJsonObject> OObj = MakeShareable(new FJsonObject());
                    OObj->SetStringField(TEXT("ObjectiveTag"), Obj.ObjectiveTag.ToString());
                    OObj->SetNumberField(TEXT("Progress"), Obj.CurrentProgress);
                    OObj->SetNumberField(TEXT("Required"), Obj.RequiredCount);
                    OObj->SetBoolField(TEXT("IsComplete"), Obj.bIsComplete);
                    ObjArray.Add(MakeShareable(new FJsonValueObject(OObj)));
                }
            }
            QObj->SetArrayField(TEXT("Objectives"), ObjArray);
            QuestsArray.Add(MakeShareable(new FJsonValueObject(QObj)));
        }
        if (QuestsArray.Num() > 0)
        {
            RootObj->SetArrayField(TEXT("ActiveQuests"), QuestsArray);
        }
    }
    
    // 8. Overheard Gossip & Regional Topics
    if (ReplicatedTopics.Items.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> GossipArray;
        for (const FDynamicTopicMemoryItem& Topic : ReplicatedTopics.Items)
        {
            TSharedPtr<FJsonObject> TopObj = MakeShareable(new FJsonObject());
            TopObj->SetStringField(TEXT("Topic"), Topic.TopicTag.ToString());
            TopObj->SetStringField(TEXT("Summary"), Topic.TopicSummary);
            TopObj->SetNumberField(TEXT("Credibility"), Topic.CredibilityScore);
            GossipArray.Add(MakeShareable(new FJsonValueObject(TopObj)));
        }
        RootObj->SetArrayField(TEXT("OverheardRumors"), GossipArray);
    }
    
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

    // 1. Resolve Best Spoken Language dynamically (Sociolinguistic Code-Switching)
    FGameplayTag SpokenLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    UDynamicLanguageComponent* SpeakerLang = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicLanguageComponent>() : nullptr;
    if (SpeakerLang)
    {
        FGameplayTagContainer ContextTags;
        if (CategoryTag.IsValid()) ContextTags.AddTag(CategoryTag);
        SpokenLanguageTag = SpeakerLang->DetermineSpokenLanguageForTarget(TargetActor, ContextTags);
    }

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

    FGameplayTag SelectedVolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
    float SelectedAudibleRadius = 1500.0f;
    FGameplayTag SelectedEmotionTag = FGameplayTag::EmptyTag;

    if (ChosenEntry->UtteranceVariations.Num() > 0)
    {
        FDynamicDialogueUtteranceDef ChosenVariation;
        float VariationWeight = 0.0f;
        if (SelectBestUtterance(ChosenEntry->UtteranceVariations, TargetActor, ChosenVariation, VariationWeight))
        {
            TemplateText = ChosenVariation.UtteranceText;
            if (ChosenVariation.GestureMontage.IsValid()) OutMontage = ChosenVariation.GestureMontage;
            if (ChosenVariation.VoiceCueTag.IsValid()) OutVoiceCueTag = ChosenVariation.VoiceCueTag;
            if (ChosenVariation.VolumeTag.IsValid()) SelectedVolumeTag = ChosenVariation.VolumeTag;
            if (ChosenVariation.BaseAudibleRadius > 0.0f) SelectedAudibleRadius = ChosenVariation.BaseAudibleRadius;
            if (ChosenVariation.EmotionTag.IsValid()) SelectedEmotionTag = ChosenVariation.EmotionTag;
        }
    }

    // 2. Dynamic Physiological Volume Modulation (Health / Stance overrides)
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            static const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Health"), false);
            if (HealthTag.IsValid())
            {
                const float CurHealth = AttrSet->GetDynamicAttributeValue(HealthTag, 0);
                if (CurHealth > 0.0f && CurHealth <= 30.0f)
                {
                    SelectedVolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Scream"), false);
                    SelectedAudibleRadius = FMath::Max(SelectedAudibleRadius, 4500.0f);
                }
            }
        }

        static const FGameplayTag StanceCrouch = FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Crouch"), false);
        if (CharOwner->ActiveMovementStateTag.MatchesTag(StanceCrouch))
        {
            SelectedVolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Whisper"), false);
            SelectedAudibleRadius = FMath::Min(SelectedAudibleRadius, 300.0f);
        }
    }

    // 3. Format Tokens and Formulate Speech
    const FString FormattedEnglish = FormatDialogueTokens(TemplateText, TargetActor, TEXT("the area"), TEXT("recently"), CustomWildcards);
    if (SpeakerLang)
    {
        OutUtterance = SpeakerLang->FormulateSpokenUtterance(SpokenLanguageTag, FormattedEnglish);
    }
    else
    {
        OutUtterance = FormattedEnglish;
    }

    OnFastPathDialogueSpoken.Broadcast(GetOwner(), TargetActor, OutUtterance);

    // 4. Acoustic Broadcast & Linguistic Exposure
    const FGameplayTag ResolvedVoice = SpeakerLang ? SpeakerLang->ResolveActiveVoiceProfile() : FGameplayTag();
    BroadcastLanguageExposureAcoustic(GetOwner(), SpokenLanguageTag, OutUtterance, SelectedAudibleRadius, SelectedVolumeTag, SelectedEmotionTag, ResolvedVoice);
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
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    // 1. Resolve Best Spoken Language dynamically (Sociolinguistic Code-Switching)
    FGameplayTag SpokenLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
    UDynamicLanguageComponent* SpeakerLang = GetOwner() ? GetOwner()->FindComponentByClass<UDynamicLanguageComponent>() : nullptr;
    if (SpeakerLang)
    {
        FGameplayTagContainer ContextTags;
        if (BehaviorDef.ActionTag.IsValid()) ContextTags.AddTag(BehaviorDef.ActionTag);
        if (BehaviorDef.BehaviorTag.IsValid()) ContextTags.AddTag(BehaviorDef.BehaviorTag);
        SpokenLanguageTag = SpeakerLang->DetermineSpokenLanguageForTarget(TargetActor, ContextTags);
    }

    FGameplayTag SelectedVolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Normal"), false);
    float SelectedAudibleRadius = 1500.0f;
    FGameplayTag SelectedEmotionTag = FGameplayTag::EmptyTag;
    FString TemplateText;
    bool bResolved = false;

    if (BehaviorDef.Utterances.Num() > 0)
    {
        FDynamicDialogueUtteranceDef ChosenUtterance;
        float OutWeight = 0.0f;
        if (SelectBestUtterance(BehaviorDef.Utterances, TargetActor, ChosenUtterance, OutWeight))
        {
            OutMontage = ChosenUtterance.GestureMontage;
            OutVoiceCueTag = ChosenUtterance.VoiceCueTag;
            if (ChosenUtterance.VolumeTag.IsValid()) SelectedVolumeTag = ChosenUtterance.VolumeTag;
            if (ChosenUtterance.BaseAudibleRadius > 0.0f) SelectedAudibleRadius = ChosenUtterance.BaseAudibleRadius;
            if (ChosenUtterance.EmotionTag.IsValid()) SelectedEmotionTag = ChosenUtterance.EmotionTag;
            TemplateText = ChosenUtterance.UtteranceText;
            bResolved = true;
        }
    }

    if (!bResolved && BehaviorDef.DialogueCategoryTag.IsValid())
    {
        if (TryResolveFastPathDialogue(BehaviorDef.DialogueCategoryTag, TargetActor, OutUtterance, OutMontage, OutVoiceCueTag, CustomWildcards, BehaviorDef.DialogueProfileTag))
        {
            return true;
        }
    }

    if (!bResolved && BehaviorDef.UtteranceTemplates.Num() > 0)
    {
        const int32 RandIdx = FMath::RandRange(0, BehaviorDef.UtteranceTemplates.Num() - 1);
        TemplateText = BehaviorDef.UtteranceTemplates[RandIdx];
        bResolved = true;
    }

    if (!bResolved) return false;

    // 2. Dynamic Physiological Volume Modulation (Health / Stance overrides)
    if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (const UDynamicAttributeSet* AttrSet = CharOwner->GetDynamicAttributeSet())
        {
            static const FGameplayTag HealthTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Health"), false);
            if (HealthTag.IsValid())
            {
                const float CurHealth = AttrSet->GetDynamicAttributeValue(HealthTag, 0);
                if (CurHealth > 0.0f && CurHealth <= 30.0f)
                {
                    SelectedVolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Scream"), false);
                    SelectedAudibleRadius = FMath::Max(SelectedAudibleRadius, 4500.0f);
                }
            }
        }

        static const FGameplayTag StanceCrouch = FGameplayTag::RequestGameplayTag(TEXT("State.Moving.Crouch"), false);
        if (CharOwner->ActiveMovementStateTag.MatchesTag(StanceCrouch))
        {
            SelectedVolumeTag = FGameplayTag::RequestGameplayTag(TEXT("Dialogue.Volume.Whisper"), false);
            SelectedAudibleRadius = FMath::Min(SelectedAudibleRadius, 300.0f);
        }
    }

    // 3. Format Tokens and Formulate Speech
    const FString FormattedEnglish = FormatDialogueTokens(TemplateText, TargetActor, TEXT("the area"), TEXT("recently"), CustomWildcards);
    if (SpeakerLang)
    {
        OutUtterance = SpeakerLang->FormulateSpokenUtterance(SpokenLanguageTag, FormattedEnglish);
    }
    else
    {
        OutUtterance = FormattedEnglish;
    }

    OnFastPathDialogueSpoken.Broadcast(GetOwner(), TargetActor, OutUtterance);

    // 4. Acoustic Broadcast & Linguistic Exposure
    const FGameplayTag ResolvedVoice = SpeakerLang ? SpeakerLang->ResolveActiveVoiceProfile() : FGameplayTag();
    BroadcastLanguageExposureAcoustic(GetOwner(), SpokenLanguageTag, OutUtterance, SelectedAudibleRadius, SelectedVolumeTag, SelectedEmotionTag, ResolvedVoice);
    return true;
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

void UDynamicSocialComponent::HandleHeardDialogueFromSpeaker(AActor* Speaker, const FString& UtteranceText, FGameplayTag SpokenLanguageTag, FGameplayTag VolumeTag)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority() || !Speaker) return;

    APawn* PawnOwner = Cast<APawn>(OwnerActor);
    if (!PawnOwner || PawnOwner->IsPlayerControlled()) return;

    // 1. Update native conversational and attention memory on the Social Component directly
    ActiveInterlocutor = Speaker;
    ActiveTurnState = EConversationalTurnState::Engaged;

    // 2. Optional backwards compatibility: update Blackboard only if one exists
    if (AAIController* AICon = Cast<AAIController>(PawnOwner->GetController()))
    {
        if (UBlackboardComponent* BB = AICon->GetBlackboardComponent())
        {
            static const FName InterlocutorKeyName = TEXT("InterlocutorActor");
            static const FName TargetActorKeyName = TEXT("TargetActor");
            static const FName HeardTextKeyName = TEXT("LastHeardUtterance");

            BB->SetValueAsObject(InterlocutorKeyName, Speaker);
            BB->SetValueAsObject(TargetActorKeyName, Speaker);
            BB->SetValueAsString(HeardTextKeyName, UtteranceText);
        }
    }

    // 3. Trigger conversational response behavior in State Tree / GAS
    if (UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>())
    {
        static const FGameplayTag ChatActionTag = FGameplayTag::RequestGameplayTag(TEXT("Action.Interact.Chat"), false);
        if (ChatActionTag.IsValid())
        {
            ASC->AddLooseGameplayTags(FGameplayTagContainer(ChatActionTag));
        }
    }
}

void UDynamicSocialComponent::ServerBroadcastStreamingDialogueChunk(
    const FString& FormulatedChunk, 
    FGameplayTag SpokenLanguageTag, 
    FGameplayTag VolumeTag, 
    float BaseAudibleRadius, 
    bool bIsFinalChunk, 
    FGameplayTag EmotionTag)
{
    AActor* Speaker = GetOwner();
    UWorld* World = GetWorld();
    if (!Speaker || !Speaker->HasAuthority() || FormulatedChunk.IsEmpty() || !World) return;

    const float QueryRadius = FMath::Clamp(BaseAudibleRadius * 4.5f, 300.0f, 15000.0f);
    TArray<FOverlapResult> Overlaps;
    FCollisionShape Sphere = FCollisionShape::MakeSphere(QueryRadius);
    
    // We only care about hitting Pawns (Players) for the UI streaming effect
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(StreamingAcousticSweep), false, Speaker);

    const FVector SpeakerLoc = Speaker->GetActorLocation();

    if (World->OverlapMultiByChannel(Overlaps, SpeakerLoc, FQuat::Identity, ECC_Pawn, Sphere, QueryParams))
    {
        for (const FOverlapResult& Overlap : Overlaps)
        {
            AActor* Listener = Overlap.GetActor();
            if (!Listener || Listener == Speaker) continue;

            APawn* ListenerPawn = Cast<APawn>(Listener);
            if (ListenerPawn && ListenerPawn->IsPlayerControlled())
            {
                if (UDynamicLanguageComponent* ListenerLang = Listener->FindComponentByClass<UDynamicLanguageComponent>())
                {
                    const float Distance = FVector::Dist(SpeakerLoc, Listener->GetActorLocation());

                    // Evaluates listener hearing attributes, buffs, and wall occlusion dynamically
                    if (ListenerLang->CanHearUtteranceAtDistance(Distance, BaseAudibleRadius, VolumeTag, SpeakerLoc))
                    {
                        // Send the 7-argument RPC to the client for real-time UI typing & visemes
                        ListenerLang->ClientReceiveHeardDialogueChunk(
                            Speaker, 
                            FormulatedChunk, 
                            SpokenLanguageTag, 
                            BaseAudibleRadius, 
                            VolumeTag, 
                            bIsFinalChunk, 
                            EmotionTag
                        );
                    }
                }
            }
        }
    }
}

void UDynamicSocialComponent::ApplySocialReactionConsequences(const FDynamicDialogueReactionRule& Rule, AActor* TargetOffender)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority()) return;

    // 1. Apply Self Attribute/Need changes
    for (const auto& Pair : Rule.SelfAttributeDeltas.LocalLookupMap)
    {
        if (!Pair.Key.IsValid()) continue;

        float CurrentNeedVal = 0.0f;
        if (GetNeedValue(Pair.Key, CurrentNeedVal))
        {
            ServerSetNeedValue(Pair.Key, CurrentNeedVal + Pair.Value);
        }
        else if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(OwnerActor))
        {
            if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet()))
            {
                const float CurVal = AttrSet->GetDynamicAttributeValue(Pair.Key, 0);
                AttrSet->SetDynamicAttributeValue(Pair.Key, CurVal + Pair.Value, 0);
            }
        }
    }

    // 2. Apply Relational Metric changes towards offender
    if (TargetOffender && TargetOffender != OwnerActor && Rule.RelationalMetricDeltas.ReplicatedEntries.Num() > 0)
    {
        const UDynamicSocialComponent* OffenderSocial = TargetOffender->FindComponentByClass<UDynamicSocialComponent>();
        if (OffenderSocial && OffenderSocial->CharacterPersistentGuid.IsValid())
        {
            ServerRecordSocialInteraction(
                OffenderSocial->CharacterPersistentGuid,
                TargetOffender,
                ERelationshipTargetKind::Character,
                FGameplayTag::RequestGameplayTag(TEXT("Relationship.Type.Rivalry"), false),
                Rule.RelationalMetricDeltas.ReplicatedEntries,
                FGameplayTagContainer()
            );
        }
    }

    // 3. Optional Reaction Dialogue
    if (Rule.ReactionDialogueCategoryTag.IsValid())
    {
        FString OutUtterance;
        TSoftObjectPtr<UAnimMontage> OutMontage;
        FGameplayTag OutVoice;
        TryResolveFastPathDialogue(Rule.ReactionDialogueCategoryTag, TargetOffender, OutUtterance, OutMontage, OutVoice);
    }

    // 4. Optional Reaction Behavior: Engage state tree or GAS tag
    if (Rule.ReactionBehaviorTag.IsValid())
    {
        if (UAbilitySystemComponent* ASC = OwnerActor->FindComponentByClass<UAbilitySystemComponent>())
        {
            ASC->AddLooseGameplayTags(FGameplayTagContainer(Rule.ReactionBehaviorTag));
        }
        if (APawn* PawnOwner = Cast<APawn>(OwnerActor))
        {
            if (AAIController* AICon = Cast<AAIController>(PawnOwner->GetController()))
            {
                if (UBlackboardComponent* BB = AICon->GetBlackboardComponent())
                {
                    if (TargetOffender) BB->SetValueAsObject(FName("ThreatActor"), TargetOffender);
                }
            }
        }
    }
}

void UDynamicSocialComponent::HandleCalloutIgnored()
{
    AActor* Offender = ActiveCalloutTarget.Get();
    ClearActiveCallout();

    if (!Offender) return;

    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (const UDynamicCustomizationComponent* Cust = CharOwner->GetCustomizationComponent())
            {
                FCharacterTemplateDef TemplateDef;
                if (GS->GetCharacterTemplate(Cust->ActiveCharacterTemplateTag, TemplateDef))
                {
                    ApplySocialReactionConsequences(TemplateDef.PersonalityProfile.OnIgnoredReaction, Offender);
                }
            }
        }
    }
}

void UDynamicSocialComponent::ServerHandleSpeechInterrupted(AActor* Interrupter)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !Interrupter || Interrupter == GetOwner()) return;

    ClearActiveCallout();

    // 1. Instantly abort current Audio, TTS Queue, and Lip Sync
    if (UDynamicLanguageComponent* LangComp = GetOwner()->FindComponentByClass<UDynamicLanguageComponent>())
    {
        LangComp->InterruptActiveSpeech();
    }

    // 2. Clear Conversational Lock
    ServerReleaseConversationalLock();

    // 3. Process Psychological/Relational Consequences (Affinity loss, Hostility gain)
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        if (const ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (const UDynamicCustomizationComponent* Cust = CharOwner->GetCustomizationComponent())
            {
                FCharacterTemplateDef TemplateDef;
                if (GS->GetCharacterTemplate(Cust->ActiveCharacterTemplateTag, TemplateDef))
                {
                    ApplySocialReactionConsequences(TemplateDef.PersonalityProfile.OnInterruptedReaction, Interrupter);
                }
            }
        }
    }
}

void UDynamicSocialComponent::TriggerMemoryConsolidation(const FGuid& TargetEntityGuid, const TArray<FDynamicDialogueTurn>& TurnsToCompress)
{
    if (!GetOwner()->HasAuthority() || TurnsToCompress.Num() == 0 || !TargetEntityGuid.IsValid()) return;

    UWorld* World = GetWorld();
    UDynamicCloudAISubsystem* CloudSubsystem = World ? World->GetSubsystem<UDynamicCloudAISubsystem>() : nullptr;
    if (!CloudSubsystem) return;

    FDynamicRelationshipEdgeItem* Edge = nullptr;
    for (FDynamicRelationshipEdgeItem& ExistingEdge : ReplicatedRelationships.Items)
    {
        if (ExistingEdge.TargetEntityGuid == TargetEntityGuid)
        {
            Edge = &ExistingEdge;
            break;
        }
    }

    FString CurrentSummary = Edge ? Edge->LongTermMemorySummary : TEXT("");

    // Package the summarization prompt payload
    TSharedRef<FJsonObject> RootObj = MakeShareable(new FJsonObject());
    RootObj->SetStringField(TEXT("SystemInstruction"), 
        TEXT("You are a memory consolidation core. Condense the 'RecentConversation' and the 'OldMemorySummary' into a new, updated 'OldMemorySummary'. ")
        TEXT("Write EXACTLY 1 to 3 concise sentences summarizing what you know about this person. Return ONLY a JSON object matching: {\"NewMemorySummary\":\"<summary>\"}")
    );
    RootObj->SetStringField(TEXT("OldMemorySummary"), CurrentSummary);

    TArray<TSharedPtr<FJsonValue>> HistoryArray;
    for (const FDynamicDialogueTurn& Turn : TurnsToCompress)
    {
        TSharedPtr<FJsonObject> TurnObj = MakeShareable(new FJsonObject());
        TurnObj->SetStringField(TEXT("Actor"), Turn.SpeakerName);
        TurnObj->SetStringField(TEXT("ActionOrSpeech"), Turn.UtteranceText);
        HistoryArray.Add(MakeShareable(new FJsonValueObject(TurnObj)));
    }
    RootObj->SetArrayField(TEXT("RecentConversation"), HistoryArray);

    FString PromptJson;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PromptJson);
    FJsonSerializer::Serialize(RootObj, Writer);

    // Define the async callback to apply the newly minted memory summary directly to the relationship edge
    FOnCloudAIResponseDirectCallback ConsolidationCallback;
    ConsolidationCallback.BindLambda([this, TargetEntityGuid](const TArray<FCloudAICommandOutput>& Commands)
    {
        if (Commands.Num() > 0 && !Commands[0].UtteranceText.IsEmpty())
        {
            // We overloaded UtteranceText in the command output to hold the new memory string for this specific task
            for (FDynamicRelationshipEdgeItem& EdgeItem : ReplicatedRelationships.Items)
            {
                if (EdgeItem.TargetEntityGuid == TargetEntityGuid)
                {
                    EdgeItem.LongTermMemorySummary = Commands[0].UtteranceText;
                    ReplicatedRelationships.MarkItemDirty(EdgeItem);
                    break;
                }
            }
        }
    });

    // Dispatch a Low-Priority background LLM task (doesn't steal fast miners from real-time chat)
    CloudSubsystem->DispatchDistributedAIRequest(
        Cast<APawn>(GetOwner()),
        ECloudAIRequestType::MemoryConsolidation,
        ECloudAITaskPriority::Low, // Let background miners handle this to keep chat fast
        PromptJson,
        30.0f, // 30-second generous timeout for background tasks
        FOnCloudAITokenStream(),
        ConsolidationCallback,
        FOnCloudAITaskAssigned()
    );
}

void FDynamicTopicMemoryItem::PostReplicatedAdd(const FFastTopicMemoryArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildTopicCache();
}

void FDynamicTopicMemoryItem::PostReplicatedChange(const FFastTopicMemoryArray& InArrayOwner)
{
    if (InArrayOwner.OwningSocialComp) InArrayOwner.OwningSocialComp->RebuildTopicCache();
}

void UDynamicSocialComponent::OnRep_ReplicatedTopics()
{
    RebuildTopicCache();
}

void UDynamicSocialComponent::HandleSingleTopicReplicated(const FDynamicTopicMemoryItem& Item)
{
    RebuildTopicCache();
}

void UDynamicSocialComponent::RebuildTopicCache()
{
    CachedTopicIndices.Empty(ReplicatedTopics.Items.Num());
    for (int32 i = 0; i < ReplicatedTopics.Items.Num(); ++i)
    {
        if (ReplicatedTopics.Items[i].TopicTag.IsValid())
        {
            CachedTopicIndices.Add(ReplicatedTopics.Items[i].TopicTag, i);
        }
    }
}

void UDynamicSocialComponent::OnRep_ActiveTurnState()
{
    OnConversationalLockStateChanged.Broadcast(ActiveTurnState, ActiveInterlocutor.Get());
}

bool UDynamicSocialComponent::ServerAcquireConversationalLock(AActor* InterlocutorActor, bool bPondering)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !InterlocutorActor) return false;

    // If currently talking to someone else, reject lock acquisition unless it's the same partner
    if (ActiveTurnState != EConversationalTurnState::Idle && ActiveInterlocutor.IsValid() && ActiveInterlocutor.Get() != InterlocutorActor)
    {
        return false;
    }

    ActiveInterlocutor = InterlocutorActor;
    ActiveTurnState = bPondering ? EConversationalTurnState::Pondering : EConversationalTurnState::Engaged;
    OnRep_ActiveTurnState();
    return true;
}

void UDynamicSocialComponent::ServerReleaseConversationalLock()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    ActiveTurnState = EConversationalTurnState::Idle;
    ActiveInterlocutor.Reset();
    OnRep_ActiveTurnState();
}

void UDynamicSocialComponent::ServerRecordOverheardTopic(
    FGameplayTag TopicTag, 
    const FString& TopicSummary, 
    const FGuid& OriginatorGuid, 
    float Credibility)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TopicTag.IsValid() || TopicSummary.IsEmpty()) return;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

    if (const int32* FoundIdx = CachedTopicIndices.Find(TopicTag))
    {
        if (ReplicatedTopics.Items.IsValidIndex(*FoundIdx))
        {
            FDynamicTopicMemoryItem& Existing = ReplicatedTopics.Items[*FoundIdx];
            Existing.TopicSummary = TopicSummary;
            Existing.CredibilityScore = FMath::Clamp(Credibility, 0.0f, 1.0f);
            Existing.OverheardWorldTime = Now;
            ReplicatedTopics.MarkItemDirty(Existing);
            return;
        }
    }

    FDynamicTopicMemoryItem NewTopic;
    NewTopic.TopicTag = TopicTag;
    NewTopic.TopicSummary = TopicSummary;
    NewTopic.OriginatorGuid = OriginatorGuid;
    NewTopic.CredibilityScore = FMath::Clamp(Credibility, 0.0f, 1.0f);
    NewTopic.OverheardWorldTime = Now;

    FDynamicTopicMemoryItem& Added = ReplicatedTopics.Items.Add_GetRef(NewTopic);
    ReplicatedTopics.MarkItemDirty(Added);

    // Keep active topic memory bounded to top 8 most recent rumors
    while (ReplicatedTopics.Items.Num() > 8)
    {
        ReplicatedTopics.Items.RemoveAt(0);
        ReplicatedTopics.MarkArrayDirty();
    }

    RebuildTopicCache();
}

void UDynamicSocialComponent::ServerEvaluateDailySchedule(float CurrentGameTimeHours)
{
    if (!GetOwner()->HasAuthority()) return;
    
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    FGameplayTag HighestPriorityScheduleTag = FGameplayTag::EmptyTag;
    float BestAdherenceWeight = -MAX_flt;

    for (const FDynamicAssignedProfessionItem& ProfItem : ReplicatedProfessions.Items)
    {
        if (const FDynamicProfessionDef* ProfDef = GS->ProfessionMasterRegistry.Find(ProfItem.ProfessionTag))
        {
            for (const FDynamicScheduleBlockDef& Block : ProfDef->DailySchedule)
            {
                const bool bWrapsMidnight = Block.StartHour24 > Block.EndHour24;
                const bool bIsActiveTime = bWrapsMidnight 
                    ? (CurrentGameTimeHours >= Block.StartHour24 || CurrentGameTimeHours <= Block.EndHour24)
                    : (CurrentGameTimeHours >= Block.StartHour24 && CurrentGameTimeHours <= Block.EndHour24);

                if (bIsActiveTime)
                {
                    const float TotalWeight = Block.ScheduleAdherenceWeight * ProfItem.ProfessionPriorityWeight;
                    if (TotalWeight > BestAdherenceWeight)
                    {
                        BestAdherenceWeight = TotalWeight;
                        HighestPriorityScheduleTag = Block.ScheduledActivityTag;
                    }
                }
            }
        }
    }

    if (HighestPriorityScheduleTag.IsValid() && HighestPriorityScheduleTag != ActiveScheduleStateTag)
    {
        ActiveScheduleStateTag = HighestPriorityScheduleTag;
        
        if (UDynamicStateTreeComponent* StateTreeComp = GetOwner()->FindComponentByClass<UDynamicStateTreeComponent>())
        {
            FDynamicStateTreeEvent ScheduleEvent;
            ScheduleEvent.EventTag = HighestPriorityScheduleTag;
            ScheduleEvent.EventInstigator = GetOwner();
            StateTreeComp->SendStateTreeEvent(ScheduleEvent);
        }
    }
}