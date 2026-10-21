// DynamicForceComponent.cpp
#include "DynamicForceComponent.h"
#include "DynamicCharacter.h"
#include "DynamicPhysicsActor.h"
#include "DynamicGameState.h"
#include "Net/UnrealNetwork.h"
#include "Components/SkeletalMeshComponent.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/LayeredMoves/LaunchMove.h"

UDynamicForceComponent::UDynamicForceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PendingImpulseVector = FVector::ZeroVector;
    bHasPendingImpulse = false;
    ReplicatedHitBuffer.OwnerForceComponent = this;
}

void UDynamicForceComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedHitBuffer.OwnerForceComponent = this;

    if (AActor* Owner = GetOwner())
    {
        CachedMoverComp = Owner->FindComponentByClass<UMoverComponent>();
    }
}

void UDynamicForceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicForceComponent, ReplicatedHitBuffer);
}

void UDynamicForceComponent::QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity)
{
    if (ImpulseDirection.IsZero() || Intensity <= 0.0f) return;

    const FVector NormalizedDir = ImpulseDirection.GetSafeNormal();
    const FVector LaunchVel = NormalizedDir * Intensity;

    AActor* OwnerActor = GetOwner();
    APawn* PawnOwner = Cast<APawn>(OwnerActor);

    // 1. Locally Controlled Autonomous Client:
    // Execute QueueLayeredMove immediately on local MoverComponent for client prediction
    if (PawnOwner && PawnOwner->IsLocallyControlled())
    {
        if (CachedMoverComp.IsValid())
        {
            TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
            LaunchMove->LaunchVelocity = LaunchVel;
            LaunchMove->DurationMs = 250.0f;
            CachedMoverComp->QueueLayeredMove(LaunchMove);
        }

        // Send input intent to server
        if (!OwnerActor->HasAuthority())
        {
            Server_QueueInstantMovementImpulse(NormalizedDir, Intensity);
        }
        return;
    }

    // 2. Server Authority Execution:
    if (OwnerActor && OwnerActor->HasAuthority())
    {
        if (CachedMoverComp.IsValid())
        {
            TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
            LaunchMove->LaunchVelocity = LaunchVel;
            LaunchMove->DurationMs = 250.0f;
            CachedMoverComp->QueueLayeredMove(LaunchMove);
        }

        // If target is a remote client player, sync matching predicted move
        if (PawnOwner && PawnOwner->IsPlayerControlled() && !PawnOwner->IsLocallyControlled())
        {
            Client_ReceivePredictedLaunch(LaunchVel);
        }
    }
}

FVector UDynamicForceComponent::ConsumePendingImpulse()
{
    if (!bHasPendingImpulse) return FVector::ZeroVector;
    const FVector ReturnForce = PendingImpulseVector;
    PendingImpulseVector = FVector::ZeroVector;
    bHasPendingImpulse = false;
    return ReturnForce;
}

void UDynamicForceComponent::ServerApplyLimbForce(uint8 LimbId, const FName& BoneName, float ClampedImpulse, const FVector& HitLocation, const FVector& HitNormal)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Owner->HasAuthority()) return;

    ADynamicGameState* DynamicGS = GetWorld()->GetGameState<ADynamicGameState>();
    const FPhysicsLimbConfig* LimbData = DynamicGS ? DynamicGS->GetLimbData(LimbId) : nullptr;
    const float ImpulseMult = LimbData ? LimbData->ImpulseMultiplier : 1.0f;
    uint8 ResolvedState = 0;

    if (ClampedImpulse >= (LimbData ? LimbData->BreakRagdollThreshold : 1600.0f)) ResolvedState = 2;
    else if (ClampedImpulse >= (LimbData ? LimbData->StumbleThreshold : 800.0f)) ResolvedState = 1;

    if (ADynamicCharacter* CharacterOwner = Cast<ADynamicCharacter>(Owner))
    {
        if (ResolvedState == 2)
        {
            CharacterOwner->StartRagdoll();
        }

        if (USkeletalMeshComponent* MeshComp = CharacterOwner->GetMeshComponent())
        {
            MeshComp->AddImpulseAtLocation((-HitNormal) * (ClampedImpulse * ImpulseMult), HitLocation, BoneName);
        }
    }
    else if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Owner))
    {
        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
        {
            Prim->AddImpulseAtLocation((-HitNormal) * (ClampedImpulse * ImpulseMult), HitLocation, BoneName);
        }
    }

    FFastPhysicsHitRepItem NewHit;
    NewHit.TargetLimbId = LimbId;
    NewHit.PackedImpulseMagnitude = static_cast<uint16>(FMath::Clamp(FMath::RoundToInt(ClampedImpulse), 0, 65535));
    NewHit.HitNormal = HitNormal;
    NewHit.HitLocation = HitLocation;
    NewHit.ReactionState = ResolvedState;

    if (ReplicatedHitBuffer.Items.Num() < MaxReplicatedHitBufferSize)
    {
        FFastPhysicsHitRepItem& Added = ReplicatedHitBuffer.Items.Add_GetRef(NewHit);
        ReplicatedHitBuffer.MarkItemDirty(Added);
    }
    else
    {
        FFastPhysicsHitRepItem& Existing = ReplicatedHitBuffer.Items[WriteIndex];
        Existing = NewHit;
        ReplicatedHitBuffer.MarkItemDirty(Existing);
    }
    WriteIndex = (WriteIndex + 1) % MaxReplicatedHitBufferSize;
}

void UDynamicForceComponent::OnClientReceiveAuthoritativeHit(const FFastPhysicsHitRepItem& HitItem)
{
    AActor* Owner = GetOwner();
    if (!Owner || Owner->HasAuthority()) return;

    if (ADynamicCharacter* CharacterOwner = Cast<ADynamicCharacter>(Owner))
    {
        if (HitItem.ReactionState == 2)
        {
            CharacterOwner->StartRagdoll();
        }

        if (CharacterOwner->IsLocallyControlled() && HitItem.ReactionState < 2)
        {
            return;
        }

        if (USkeletalMeshComponent* MeshComp = CharacterOwner->GetMeshComponent())
        {
            const FVector DirectedImpulse = (-FVector(HitItem.HitNormal)) * static_cast<float>(HitItem.PackedImpulseMagnitude);
            MeshComp->AddImpulseAtLocation(DirectedImpulse, FVector(HitItem.HitLocation));
        }
    }
    else if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Owner))
    {
        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
        {
            const FVector DirectedImpulse = (-FVector(HitItem.HitNormal)) * static_cast<float>(HitItem.PackedImpulseMagnitude);
            Prim->AddImpulseAtLocation(DirectedImpulse, FVector(HitItem.HitLocation));
        }
    }
}

bool UDynamicForceComponent::Server_QueueInstantMovementImpulse_Validate(FVector_NetQuantizeNormal ImpulseDirection, float Intensity)
{
    return !ImpulseDirection.IsZero() && Intensity >= 0.0f && Intensity <= 25000.0f;
}

void UDynamicForceComponent::Server_QueueInstantMovementImpulse_Implementation(FVector_NetQuantizeNormal ImpulseDirection, float Intensity)
{
    const float ClampedIntensity = FMath::Clamp(Intensity, 0.0f, 25000.0f);
    const FVector LaunchVel = FVector(ImpulseDirection) * ClampedIntensity;

    if (CachedMoverComp.IsValid())
    {
        TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
        LaunchMove->LaunchVelocity = LaunchVel;
        LaunchMove->DurationMs = 250.0f;
        CachedMoverComp->QueueLayeredMove(LaunchMove);
    }
}

void UDynamicForceComponent::Client_ReceivePredictedLaunch_Implementation(FVector_NetQuantize LaunchVelocity)
{
    AActor* OwnerActor = GetOwner();
    APawn* PawnOwner = Cast<APawn>(OwnerActor);
    if (!PawnOwner || !PawnOwner->IsLocallyControlled()) return;

    if (CachedMoverComp.IsValid())
    {
        TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
        LaunchMove->LaunchVelocity = FVector(LaunchVelocity);
        LaunchMove->DurationMs = 250.0f;
        CachedMoverComp->QueueLayeredMove(LaunchMove);
    }
}

void UDynamicForceComponent::ApplyPredictiveLimbImpulse(const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal)
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    const FVector ImpulseVector = (-HitNormal) * ImpulseMagnitude;

    if (ADynamicCharacter* CharacterOwner = Cast<ADynamicCharacter>(Owner))
    {
        if (CachedMoverComp.IsValid() && CharacterOwner->IsLocallyControlled())
        {
            TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
            LaunchMove->LaunchVelocity = ImpulseVector;
            LaunchMove->DurationMs = 150.0f;
            CachedMoverComp->QueueLayeredMove(LaunchMove);
        }

        if (USkeletalMeshComponent* MeshComp = CharacterOwner->GetMeshComponent())
        {
            MeshComp->AddImpulseAtLocation(ImpulseVector, HitLocation, BoneName);
        }
    }
    else if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Owner))
    {
        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
        {
            Prim->AddImpulseAtLocation(ImpulseVector, HitLocation, BoneName);
        }
    }
}

void FFastPhysicsHitRepItem::PostReplicatedAdd(const FFastPhysicsHitRepArray& InArrayOwner)
{
    if (InArrayOwner.OwnerForceComponent)
    {
        InArrayOwner.OwnerForceComponent->OnClientReceiveAuthoritativeHit(*this);
    }
}

void FFastPhysicsHitRepItem::PostReplicatedChange(const FFastPhysicsHitRepArray& InArrayOwner)
{
    if (InArrayOwner.OwnerForceComponent)
    {
        InArrayOwner.OwnerForceComponent->OnClientReceiveAuthoritativeHit(*this);
    }
}