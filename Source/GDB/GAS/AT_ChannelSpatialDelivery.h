// AT_ChannelSpatialDelivery.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "DynamicGASTypes.h"
#include "AT_ChannelSpatialDelivery.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChannelTaskCompletedSignature, bool, bCompletedSuccessfully);

UCLASS()
class GDB_API UAT_ChannelSpatialDelivery : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAT_ChannelSpatialDelivery(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintAssignable)
	FChannelTaskCompletedSignature OnChannelFinished;

	UFUNCTION(BlueprintCallable, Category = "Ability|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UAT_ChannelSpatialDelivery* ChannelSpatialDelivery(UGameplayAbility* OwningAbility, FName EffectorBone);

	virtual void Activate() override;
	virtual void TickTask(float DeltaTime) override;
	virtual void OnDestroy(bool bInOwnerFinished) override;

private:
	FName SourceEffectorBone;
	float ElapsedTime = 0.0f;
	float TimeSinceLastDamageTick = 0.0f;
	FGuid ActiveChannelVfxGuid;

	void ProcessBeamChannel(float DeltaTime, const FDynamicAbilityDef& Config, AActor* Avatar);
	void ProcessConeSprayChannel(float DeltaTime, const FDynamicAbilityDef& Config, AActor* Avatar);
	void ApplyDamageToTarget(AActor* Target, const FVector& HitLocation, const FVector& HitNormal, const FDynamicAbilityDef& Config);
};