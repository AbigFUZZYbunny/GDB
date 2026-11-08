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
    VirtualNPCSpatialHash.Empty();
    ActiveNPCs.Empty();
    Super::Deinitialize();
}

TStatId UDynamicWorldDirectorSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDynamicWorldDirectorSubsystem, STATGROUP_Tickables);
}

void UDynamicWorldDirectorSubsystem::Tick(float DeltaTime)
{
    if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;

    ProximityCheckAccumulator += DeltaTime;
    MacroSimulationAccumulator += DeltaTime;

    if (ProximityCheckAccumulator >= 1.0f)
    {
        PerformProximityChecks();
        ProximityCheckAccumulator = 0.0f;
    }

    if (MacroSimulationAccumulator >= 10.0f)
    {
        SimulateVirtualNPCs(MacroSimulationAccumulator);

        if (ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>())
        {
            const float CurrentGameSecs = GetWorld()->GetTimeSeconds() * GS->CalendarConfig.TimeScalar;
            const float CurrentHours24 = FMath::Fmod(CurrentGameSecs / 3600.0f, 24.0f);

            for (int32 i = ActiveNPCs.Num() - 1; i >= 0; --i)
            {
                if (ActiveNPCs[i].IsValid())
                {
                    if (UDynamicSocialComponent* Social = ActiveNPCs[i]->FindComponentByClass<UDynamicSocialComponent>())
                    {
                        Social->ServerEvaluateDailySchedule(CurrentHours24);
                    }
                }
            }
        }

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
    TArray<FVector> PlayerLocations;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (APawn* P = It->Get()->GetPawn())
        {
            PlayerLocations.Add(P->GetActorLocation());
        }
    }

    if (PlayerLocations.Num() == 0) return;

    const float WakeDistSq = FMath::Square(InstantiationRadius);
    const float SleepDistSq = FMath::Square(HibernationRadius);

    // 1. Check Active NPCs for Hibernation
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

        for (const FVector& PLoc : PlayerLocations)
        {
            if (FVector::DistSquared(NPCLoc, PLoc) < SleepDistSq)
            {
                bShouldHibernate = false;
                break;
            }
        }

        if (bShouldHibernate)
        {
            VirtualizeNPC(NPC);
            ActiveNPCs.RemoveAtSwap(i);
        }
    }

    // 2. Check Virtual NPCs for Instantiation (Using O(1) Spatial Hash Grid Lookups)
    TSet<FGuid> NPCsToWake;
    for (const FVector& PLoc : PlayerLocations)
    {
        const FIntPoint CenterCell(
            FMath::FloorToInt(PLoc.X / VirtualSpatialGridSize),
            FMath::FloorToInt(PLoc.Y / VirtualSpatialGridSize)
        );

        // Check the 3x3 surrounding spatial buckets
        for (int32 X = -1; X <= 1; ++X)
        {
            for (int32 Y = -1; Y <= 1; ++Y)
            {
                if (const TSet<FGuid>* Bucket = VirtualNPCSpatialHash.Find(CenterCell + FIntPoint(X, Y)))
                {
                    for (const FGuid& Guid : *Bucket)
                    {
                        if (const FVirtualNPCRecord* Rec = VirtualNPCRegistry.Find(Guid))
                        {
                            if (FVector::DistSquared(Rec->WorldLocation, PLoc) < WakeDistSq)
                            {
                                NPCsToWake.Add(Guid);
                            }
                        }
                    }
                }
            }
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

    if (const UDynamicCustomizationComponent* Cust = NPC->GetCustomizationComponent())
    {
        Record.CharacterTemplateTag = Cust->ActiveCharacterTemplateTag;
    }
    
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

    if (UDynamicDataSerializer::SerializePlayerToSnapshotJson(NPC, Record.SerializedPersistenceJson))
    {
        VirtualNPCRegistry.Add(Record.NPCGuid, Record);
        
        // Register to Spatial Hash
        const FIntPoint Cell(
            FMath::FloorToInt(Record.WorldLocation.X / VirtualSpatialGridSize),
            FMath::FloorToInt(Record.WorldLocation.Y / VirtualSpatialGridSize)
        );
        VirtualNPCSpatialHash.FindOrAdd(Cell).Add(Record.NPCGuid);
        
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

    // Scrub from Spatial Hash
    const FIntPoint Cell(
        FMath::FloorToInt(Record.WorldLocation.X / VirtualSpatialGridSize),
        FMath::FloorToInt(Record.WorldLocation.Y / VirtualSpatialGridSize)
    );
    if (TSet<FGuid>* Bucket = VirtualNPCSpatialHash.Find(Cell))
    {
        Bucket->Remove(NPCGuid);
    }

    ADynamicGameState* GS = GetWorld()->GetGameState<ADynamicGameState>();
    if (!GS) return nullptr;

    FTransform SpawnTransform(Record.WorldRotation, Record.WorldLocation);
    ADynamicCharacter* WokenNPC = GS->AcquireCharacter(Record.CharacterTemplateTag, SpawnTransform, nullptr, nullptr);
    if (!WokenNPC) return nullptr;

    if (UDynamicDataSerializer::DeserializePlayerFromSnapshotJson(WokenNPC, Record.SerializedPersistenceJson))
    {
        ActiveNPCs.Add(WokenNPC);
        
        const double SleepDuration = GetWorld()->GetTimeSeconds() - Record.HibernationStartWorldTime;
        PerformCatchUpSimulation(WokenNPC, SleepDuration);

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
    if (VirtualNPCRegistry.Num() == 0) return;

    TWeakObjectPtr<UDynamicWorldDirectorSubsystem> WeakThis(this);
    TSharedRef<TArray<FGuid>> KeysToProcess = MakeShared<TArray<FGuid>>();
    VirtualNPCRegistry.GenerateKeyArray(KeysToProcess.Get());
    TSharedRef<int32> CurrentIndex = MakeShared<int32>(0);

    // OPTIMIZATION: Disperse math operations via 2ms time-slices across multiple frames
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, KeysToProcess, CurrentIndex, DeltaTime](float TickDelta) -> bool 
    {
        if (!WeakThis.IsValid()) return false;
        
        UDynamicWorldDirectorSubsystem* StrongThis = WeakThis.Get();
        const double StartTime = FPlatformTime::Seconds();
        constexpr double MaxBudgetPerFrameSeconds = 0.002;

        while (*CurrentIndex < KeysToProcess->Num() && (FPlatformTime::Seconds() - StartTime < MaxBudgetPerFrameSeconds))
        {
            const FGuid& NpcKey = (*KeysToProcess)[*CurrentIndex];
            if (FVirtualNPCRecord* Record = StrongThis->VirtualNPCRegistry.Find(NpcKey))
            {
                if (Record->bIsTraveling)
                {
                    const FIntPoint OldCell(
                        FMath::FloorToInt(Record->WorldLocation.X / StrongThis->VirtualSpatialGridSize),
                        FMath::FloorToInt(Record->WorldLocation.Y / StrongThis->VirtualSpatialGridSize)
                    );

                    FVector Direction = Record->TravelDestination - Record->WorldLocation;
                    float DistanceToTarget = Direction.Size();

                    if (DistanceToTarget <= (Record->TravelSpeed * DeltaTime))
                    {
                        Record->WorldLocation = Record->TravelDestination;
                        Record->bIsTraveling = false;
                    }
                    else
                    {
                        Direction.Normalize();
                        Record->WorldLocation += Direction * (Record->TravelSpeed * DeltaTime);
                        Record->WorldRotation = Direction.Rotation();
                    }

                    const FIntPoint NewCell(
                        FMath::FloorToInt(Record->WorldLocation.X / StrongThis->VirtualSpatialGridSize),
                        FMath::FloorToInt(Record->WorldLocation.Y / StrongThis->VirtualSpatialGridSize)
                    );
                    
                    if (OldCell != NewCell)
                    {
                        if (TSet<FGuid>* Bucket = StrongThis->VirtualNPCSpatialHash.Find(OldCell)) Bucket->Remove(Record->NPCGuid);
                        StrongThis->VirtualNPCSpatialHash.FindOrAdd(NewCell).Add(Record->NPCGuid);
                    }
                }

                if (StrongThis->bIsLocalCoopMode)
                {
                    // Execute simplified daily routine math natively here
                }
            }
            (*CurrentIndex)++;
        }

        return *CurrentIndex < KeysToProcess->Num();
    }));
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

    int32 RemovedCount = ActiveNPCs.RemoveAllSwap([NPC](const TWeakObjectPtr<ADynamicCharacter>& WeakNPC) {
        return !WeakNPC.IsValid() || WeakNPC.Get() == NPC;
    });

    if (RemovedCount == 0)
    {
        if (UDynamicSocialComponent* Social = NPC->FindComponentByClass<UDynamicSocialComponent>())
        {
            if (Social->CharacterPersistentGuid.IsValid())
            {
                if (const FVirtualNPCRecord* Rec = VirtualNPCRegistry.Find(Social->CharacterPersistentGuid))
                {
                    const FIntPoint Cell(
                        FMath::FloorToInt(Rec->WorldLocation.X / VirtualSpatialGridSize), 
                        FMath::FloorToInt(Rec->WorldLocation.Y / VirtualSpatialGridSize)
                    );
                    if (TSet<FGuid>* Bucket = VirtualNPCSpatialHash.Find(Cell))
                    {
                        Bucket->Remove(Social->CharacterPersistentGuid);
                    }
                }
                VirtualNPCRegistry.Remove(Social->CharacterPersistentGuid);
            }
        }
    }
}

TArray<FGuid> UDynamicWorldDirectorSubsystem::GetNPCGuidsInRadius(const FVector& SearchOrigin, float SearchRadius) const
{
    TArray<FGuid> OutGuids;
    if (SearchRadius <= 0.0f || VirtualSpatialGridSize <= 0.0f) return OutGuids;

    const FIntPoint CenterCell(
        FMath::FloorToInt(SearchOrigin.X / VirtualSpatialGridSize),
        FMath::FloorToInt(SearchOrigin.Y / VirtualSpatialGridSize)
    );

    const int32 CellRange = FMath::CeilToInt(SearchRadius / VirtualSpatialGridSize);
    const float SearchRadiusSq = FMath::Square(SearchRadius);

    for (int32 X = -CellRange; X <= CellRange; ++X)
    {
        for (int32 Y = -CellRange; Y <= CellRange; ++Y)
        {
            if (const TSet<FGuid>* Bucket = VirtualNPCSpatialHash.Find(CenterCell + FIntPoint(X, Y)))
            {
                for (const FGuid& CandidateGuid : *Bucket)
                {
                    if (const FVirtualNPCRecord* Record = VirtualNPCRegistry.Find(CandidateGuid))
                    {
                        if (FVector::DistSquared(SearchOrigin, Record->WorldLocation) <= SearchRadiusSq)
                        {
                            OutGuids.Add(CandidateGuid);
                        }
                    }
                }
            }
        }
    }

    return OutGuids;
}