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

void UDynamicWorldDirectorSubsystem::PerformCatchUpSimulation(ADynamicCharacter* NPC, float OfflineHours)
{
    if (!NPC || OfflineHours <= 0.0f) return;

    UDynamicSocialComponent* SocialComp = NPC->FindComponentByClass<UDynamicSocialComponent>();
    UDynamicAttributeSet* AttrSet = NPC->GetDynamicAttributeSet();
    if (!SocialComp || !AttrSet) return;

    for (const FDynamicNeedStateItem& Need : SocialComp->ReplicatedNeedsAndMoods.Items)
    {
        if (Need.NeedTag.IsValid() && Need.ShiftRatePerHour != 0.0f)
        {
            // Degradation is governed strictly by the JSON-defined ShiftRatePerHour configured per template/need
            const float CurrentVal = AttrSet->GetDynamicAttributeValue(Need.NeedTag, 0);
            const float DeltaShift = Need.ShiftRatePerHour * OfflineHours;
            const float NewVal = FMath::Max(0.0f, CurrentVal + DeltaShift);
            
            AttrSet->SetDynamicAttributeValue(Need.NeedTag, NewVal, 0);
        }
    }
}

void UDynamicWorldDirectorSubsystem::UnregisterActiveNPC(ADynamicCharacter* NPC)
{
    if (!NPC || !GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;

    // Remove the character from the ActiveNPCs array
    int32 RemovedCount = ActiveNPCs.RemoveAllSwap([NPC](const TWeakObjectPtr<ADynamicCharacter>& WeakNPC) {
        return !WeakNPC.IsValid() || WeakNPC.Get() == NPC;
    });

    // Optionally: If the NPC is currently hibernating (virtualized), we should ensure they 
    // are scrubbed from the virtual registry if they are being permanently destroyed.
    if (RemovedCount == 0)
    {
        if (UDynamicSocialComponent* Social = NPC->FindComponentByClass<UDynamicSocialComponent>())
        {
            if (Social->CharacterPersistentGuid.IsValid())
            {
                VirtualNPCRegistry.Remove(Social->CharacterPersistentGuid);
            }
        }
    }
}