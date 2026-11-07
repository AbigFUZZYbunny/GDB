// ============================================================================
// AnimNotify_DynamicFootstep.h
// ============================================================================
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_DynamicFootstep.generated.h"

UCLASS(const, hidecategories=Object, collapsecategories, meta=(DisplayName="Dynamic GAS Ground Impact"))
class GDB_API UAnimNotify_DynamicFootstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	UAnimNotify_DynamicFootstep();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;

	// The specific bone striking the ground (e.g., "foot_r", "shoulder_l", "pelvis")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	FName ContactBoneName = TEXT("foot_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	float TraceDownDistance = 45.0f;

	// Force multiplier to simulate hard landings, heavy stomps, or rolling body weight
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	float ImpactForceMultiplier = 1.0f;
};