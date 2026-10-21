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
    PendingImpulseVector = ImpulseDirection.GetSafeNormal() * Intensity;
    bHasPendingImpulse = true;

    // Direct Integration with Mover Component via QueueLayeredMove
    if (CachedMoverComp.IsValid())
    {
        TSharedPtr<FLayeredMove_Launch> LaunchMove = MakeShared<FLayeredMove_Launch>();
        LaunchMove->LaunchVelocity = PendingImpulseVector;
        LaunchMove->DurationMs = 250.0f; // 250ms duration

        // FLayeredMove_Launch inherits from FLayeredMoveBase.
        // QueueLayeredMove takes TSharedPtr<FLayeredMoveBase>, which implicitly converts without any casts.
        CachedMoverComp->QueueLayeredMove(LaunchMove);
        bHasPendingImpulse = false;
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

void UDynamicForceComponent::ApplyPredictiveLimbImpulse(const FName& BoneName, float ImpulseMagnitude, const FVector& HitLocation, const FVector& HitNormal)
{
    AActor* Owner = GetOwner();
    if (!Owner) return;

    if (ADynamicCharacter* CharacterOwner = Cast<ADynamicCharacter>(Owner))
    {
        if (USkeletalMeshComponent* MeshComp = CharacterOwner->GetMeshComponent())
        {
            MeshComp->AddImpulseAtLocation((-HitNormal) * ImpulseMagnitude, HitLocation, BoneName);
        }
    }
    else if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Owner))
    {
        if (UPrimitiveComponent* Prim = PhysActor->GetActivePhysicsPrimitive())
        {
            Prim->AddImpulseAtLocation((-HitNormal) * ImpulseMagnitude, HitLocation, BoneName);
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