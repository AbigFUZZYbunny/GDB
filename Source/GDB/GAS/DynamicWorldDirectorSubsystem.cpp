#include "DynamicWorldDirectorSubsystem.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicDataSerializer.h"
#include "DynamicSocialComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicEvolutionComponent.h"
#include "DynamicCustomizationComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

void UDynamicWorldDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicWorldDirectorSubsystem::Deinitialize()
{
    VirtualNPCRegistry.Empty();
    ActiveNPCs.Empty();
    Super::Deinitialize();
}

TStatId UDynamicWorldDirectorSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDynamicWorldDirectorSubsystem, STATGROUP_Tickables);
}

void UDynamicWorldDirectorSubsystem::Tick(float DeltaTime)
{
    // Fix: UWorld uses GetNetMode() instead of IsServer()
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;

    ProximityCheckAccumulator += DeltaTime;
    MacroSimulationAccumulator += DeltaTime;

    // Run spatial boundary checks every 1 second
    if (ProximityCheckAccumulator >= 1.0f)
    {
        PerformProximityChecks();
        ProximityCheckAccumulator = 0.0f;
    }

    // Run Macro Virtual Simulation every 10 seconds (Background Travel, Daily Needs)
    if (MacroSimulationAccumulator >= 10.0f)
    {
        SimulateVirtualNPCs(MacroSimulationAccumulator);
        MacroSimulationAccumulator = 0.0f;
    }
}

void UDynamicWorldDirectorSubsystem::RegisterActiveNPC(ADynamicCharacter* NPC)
{
    if (NPC && !NPC->IsPlayerControlled())
    {
        ActiveNPCs.AddUnique(NPC);
    }
}

void UDynamicWorldDirectorSubsystem::PerformProximityChecks()
{
    TArray<APlayerController*> Players;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (APlayerController* PC = It->Get()) Players.Add(PC);
    }

    if (Players.Num() == 0) return;

    // 1. Check Active NPCs for Hibernation (Exiting Radius)
    for (int32 i = ActiveNPCs.Num() - 1; i >= 0; --i)
    {
        if (!ActiveNPCs[i].IsValid())
        {
            ActiveNPCs.RemoveAtSwap(i);
            continue;
        }

        ADynamicCharacter* NPC = ActiveNPCs[i].Get();
        bool bShouldHibernate = true;
        const FVector NPCLoc = NPC->GetActorLocation();

        for (APlayerController* PC : Players)
        {
            if (APawn* PlayerPawn = PC->GetPawn())
            {
                if (FVector::DistSquared(NPCLoc, PlayerPawn->GetActorLocation()) < FMath::Square(HibernationRadius))
                {
                    bShouldHibernate = false;
                    break;
                }
            }
        }

        if (bShouldHibernate)
        {
            VirtualizeNPC(NPC);
            ActiveNPCs.RemoveAtSwap(i);
        }
    }

    // 2. Check Virtual NPCs for Instantiation (Entering Radius)
    TArray<FGuid> NPCsToWake;
    for (auto& Pair : VirtualNPCRegistry)
    {
        bool bShouldWake = false;
        for (APlayerController* PC : Players)
        {
            if (APawn* PlayerPawn = PC->GetPawn())
            {
                if (FVector::DistSquared(Pair.Value.WorldLocation, PlayerPawn->GetActorLocation()) < FMath::Square(InstantiationRadius))
                {
                    bShouldWake = true;
                    break;
                }
            }
        }

        if (bShouldWake)
        {
            NPCsToWake.Add(Pair.Key);
        }
    }

    for (const FGuid& WakeGuid : NPCsToWake)
    {
        InstantiateNPC(WakeGuid);
    }
}

void UDynamicWorldDirectorSubsystem::VirtualizeNPC(ADynamicCharacter* NPC)
{
    if (!NPC || NPC->IsPlayerControlled()) return;

    UDynamicSocialComponent* Social = NPC->FindComponentByClass<UDynamicSocialComponent>();
    if (!Social) return;

    if (!Social->CharacterPersistentGuid.IsValid())
    {
        Social->CharacterPersistentGuid = FGuid::NewGuid();
    }

    FVirtualNPCRecord Record;
    Record.NPCGuid = Social->CharacterPersistentGuid;
    Record.WorldLocation = NPC->GetActorLocation();
    Record.WorldRotation = NPC->GetActorRotation();
    Record.HibernationStartWorldTime = GetWorld()->GetTimeSeconds();

    // Fix A: Capture CharacterTemplateTag so pooled rehydration restores mesh, morphs, and stats
    if (const UDynamicCustomizationComponent* Cust = NPC->GetCustomizationComponent())
    {
        Record.CharacterTemplateTag = Cust->ActiveCharacterTemplateTag;
    }
    
    // Extract movement intent if navigating via AAIController
    if (AAIController* AICon = Cast<AAIController>(NPC->GetController()))
    {
        if (AICon->GetPathFollowingComponent() && AICon->GetPathFollowingComponent()->HasValidPath())
        {
            Record.bIsTraveling = true;
            Record.TravelDestination = AICon->GetPathFollowingComponent()->GetPathDestination();
            Record.TravelSpeed = NPC->GetVelocity().Size2D();
            if (Record.TravelSpeed <= 10.0f) Record.TravelSpeed = 300.0f; 
        }
    }

    // Capture full data payload to JSON
    if (UDynamicDataSerializer::SerializePlayerToSnapshotJson(NPC, Record.SerializedPersistenceJson))
    {
        VirtualNPCRegistry.Add(Record.NPCGuid, Record);
        
        // Return physical character to pool
        if (ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>())
        {
            GS->ReleaseCharacterToPool(NPC);
        }
        else
        {
            NPC->Destroy();
        }
    }
}

ADynamicCharacter* UDynamicWorldDirectorSubsystem::InstantiateNPC(const FGuid& NPCGuid)
{
    FVirtualNPCRecord Record;
    if (!VirtualNPCRegistry.RemoveAndCopyValue(NPCGuid, Record)) return nullptr;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return nullptr;

    FTransform SpawnTransform(Record.WorldRotation, Record.WorldLocation);
    
    // Pull from Object Pool to prevent hitching
    ADynamicCharacter* WokenNPC = GS->AcquireCharacter(Record.CharacterTemplateTag, SpawnTransform, nullptr, nullptr);
    if (!WokenNPC) return nullptr;

    // Rehydrate state
    if (UDynamicDataSerializer::DeserializePlayerFromSnapshotJson(WokenNPC, Record.SerializedPersistenceJson))
    {
        ActiveNPCs.Add(WokenNPC);
        
        const double SleepDuration = GetWorld()->GetTimeSeconds() - Record.HibernationStartWorldTime;
        PerformCatchUpSimulation(WokenNPC, SleepDuration);

        // Re-issue movement command to AAIController if traveling
        if (Record.bIsTraveling)
        {
            if (AAIController* AICon = Cast<AAIController>(WokenNPC->GetController()))
            {
                AICon->MoveToLocation(Record.TravelDestination);
            }
        }
    }

    return WokenNPC;
}

void UDynamicWorldDirectorSubsystem::SimulateVirtualNPCs(float DeltaTime)
{
    for (auto& Pair : VirtualNPCRegistry)
    {
        FVirtualNPCRecord& Record = Pair.Value;

        // 1. Process Virtual Travel
        if (Record.bIsTraveling)
        {
            FVector Direction = Record.TravelDestination - Record.WorldLocation;
            float DistanceToTarget = Direction.Size();

            if (DistanceToTarget <= (Record.TravelSpeed * DeltaTime))
            {
                Record.WorldLocation = Record.TravelDestination;
                Record.bIsTraveling = false;
            }
            else
            {
                Direction.Normalize();
                Record.WorldLocation += Direction * (Record.TravelSpeed * DeltaTime);
                Record.WorldRotation = Direction.Rotation();
            }
        }

        // 2. High-Level Needs / AI Directives (Local Co-Op macro simulation loop)
        if (bIsLocalCoopMode)
        {
            // Execute simplified daily routine math natively here
        }
    }
}

void UDynamicWorldDirectorSubsystem::PerformCatchUpSimulation(ADynamicCharacter* WokenNPC, double HibernationDurationSeconds)
{
    if (!WokenNPC || HibernationDurationSeconds <= 0.0) return;

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return;

    const float GameHoursPassed = (HibernationDurationSeconds * GS->CalendarConfig.TimeScalar) / 3600.0f;

    // Scale vital needs based on elapsed time out-of-range
    if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(WokenNPC->GetDynamicAttributeSet()))
    {
        static const FGameplayTag HungerTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Hunger"), false);
        static const FGameplayTag EnergyTag = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Resource.Energy"), false);

        if (HungerTag.IsValid())
        {
            const float CurrentHunger = AttrSet->GetDynamicAttributeValue(HungerTag, 0);
            AttrSet->SetDynamicAttributeValue(HungerTag, FMath::Max(0.0f, CurrentHunger - (10.0f * GameHoursPassed)), 0);
        }

        if (EnergyTag.IsValid())
        {
            const float CurrentEnergy = AttrSet->GetDynamicAttributeValue(EnergyTag, 0);
            AttrSet->SetDynamicAttributeValue(EnergyTag, FMath::Max(0.0f, CurrentEnergy - (15.0f * GameHoursPassed)), 0);
        }
    }

    // Fast-forward evolutionary/aging schedules
    if (UDynamicEvolutionComponent* EvoComp = WokenNPC->FindComponentByClass<UDynamicEvolutionComponent>())
    {
        EvoComp->RecalculateEvolutionSchedule();
    }
}