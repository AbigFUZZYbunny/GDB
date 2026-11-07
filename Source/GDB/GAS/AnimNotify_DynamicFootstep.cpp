// ============================================================================
// AnimNotify_DynamicFootstep.cpp
// ============================================================================
#include "AnimNotify_DynamicFootstep.h"
#include "DynamicAudioSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

UAnimNotify_DynamicFootstep::UAnimNotify_DynamicFootstep()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(190, 150, 90, 255);
#endif
}

void UAnimNotify_DynamicFootstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp) return;

	AActor* OwnerActor = MeshComp->GetOwner();
	UWorld* World = MeshComp->GetWorld();
	if (!OwnerActor || !World) return;

	UDynamicAudioSubsystem* AudioSub = World->GetSubsystem<UDynamicAudioSubsystem>();
	if (!AudioSub) return;

	const FVector SocketLoc = MeshComp->GetSocketLocation(ContactBoneName);
	const FVector TraceEnd = SocketLoc - FVector(0.0f, 0.0f, TraceDownDistance);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DynamicGroundImpactTrace), false, OwnerActor);

	if (World->LineTraceSingleByChannel(HitResult, SocketLoc + FVector(0.0f, 0.0f, 15.0f), TraceEnd, ECC_Visibility, QueryParams))
	{
		AudioSub->PlayLimbGroundAcoustics(OwnerActor, HitResult, ContactBoneName, ImpactForceMultiplier);
	}
}