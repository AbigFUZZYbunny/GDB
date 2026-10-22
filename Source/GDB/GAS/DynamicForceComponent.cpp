// DynamicForceComponent.cpp
#include "DynamicForceComponent.h"
#include "DynamicCharacter.h"
#include "DynamicPhysicsActor.h"
#include "DynamicGameState.h"
#include "Components/SkeletalMeshComponent.h"
#include "MoverComponent.h"
#include "DefaultMovementSet/LayeredMoves/LaunchMove.h"

UDynamicForceComponent::UDynamicForceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    PendingImpulseVector = FVector::ZeroVector;
    bHasPendingImpulse = false;
}

void UDynamicForceComponent::BeginPlay()
{
    Super::BeginPlay();
    if (AActor* Owner = GetOwner())
    {
        CachedMoverComp = Owner->FindComponentByClass<UMoverComponent>();
    }
}

void UDynamicForceComponent::QueueInstantMovementImpulse(FVector ImpulseDirection, float Intensity)
{
    if (ImpulseDirection.IsZero() || Intensity <= 0.0f) return;

    const FVector NormalizedDir = ImpulseDirection.GetSafeNormal();
    const FVector LaunchVel = NormalizedDir * Intensity;

    AActor* OwnerActor = GetOwner();
    APawn* PawnOwner = Cast<APawn>(OwnerActor);

    if (PawnOwner && PawnOwner->IsLocallyControlled())
    {
        if (CachedMoverComp.IsValid())
        {
            TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
            LaunchMove->LaunchVelocity = LaunchVel;
            LaunchMove->DurationMs = 250.0f;
            CachedMoverComp->QueueLayeredMove(LaunchMove);
        }

        if (!OwnerActor->HasAuthority())
        {
            Server_QueueInstantMovementImpulse(NormalizedDir, Intensity);
        }
        return;
    }

    if (OwnerActor && OwnerActor->HasAuthority())
    {
        if (CachedMoverComp.IsValid())
        {
            TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
            LaunchMove->LaunchVelocity = LaunchVel;
            LaunchMove->DurationMs = 250.0f;
            CachedMoverComp->QueueLayeredMove(LaunchMove);
        }

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

    ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FPhysicsLimbConfig* LimbData = DynamicGS ? DynamicGS->GetLimbData(LimbId) : nullptr;

    uint8 ResolvedState = 0;
    if (ClampedImpulse >= (LimbData ? LimbData->BreakRagdollThreshold : 1600.0f)) ResolvedState = 2;
    else if (ClampedImpulse >= (LimbData ? LimbData->StumbleThreshold : 800.0f)) ResolvedState = 1;

    // 1. Execute locally on server
    ApplyHitReactionLocal(LimbId, BoneName, ClampedImpulse, HitLocation, HitNormal, ResolvedState);

    // 2. Broadcast transient event to all relevancy proxies
    Multicast_PlayLimbHit(LimbId, BoneName, ClampedImpulse, HitLocation, HitNormal, ResolvedState);
}

void UDynamicForceComponent::Multicast_PlayLimbHit_Implementation(uint8 LimbId, const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal, uint8 ReactionState)
{
    // Authority has already applied the physics forces locally
    if (GetOwner() && !GetOwner()->HasAuthority())
    {
        ApplyHitReactionLocal(LimbId, BoneName, ImpulseMagnitude, HitLocation, HitNormal, ReactionState);
    }
}

void UDynamicForceComponent::ApplyHitReactionLocal(uint8 LimbId, const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal, uint8 ReactionState)
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    ADynamicGameState* DynamicGS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    const FPhysicsLimbConfig* LimbData = DynamicGS ? DynamicGS->GetLimbData(LimbId) : nullptr;
    const float ImpulseMult = LimbData ? LimbData->ImpulseMultiplier : 1.0f;
    const FVector DirectedImpulse = (-HitNormal) * (ImpulseMagnitude * ImpulseMult);

    if (ADynamicCharacter* CharacterOwner = Cast<ADynamicCharacter>(Owner))
    {
        if (ReactionState == 2)
        {
            CharacterOwner->StartRagdoll();
        }
        else if (CharacterOwner->IsLocallyControlled())
        {
            // Locally predicted clients ignore external non-ragdoll skeletal impulses to prevent jitter
            return;
        }

        if (USkeletalMeshComponent* MeshComp = CharacterOwner->GetMeshComponent())
        {
            MeshComp->AddImpulseAtLocation(DirectedImpulse, HitLocation, BoneName);
        }
    }
    else if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Owner))
    {
        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
        {
            Prim->AddImpulseAtLocation(DirectedImpulse, HitLocation, BoneName);
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