#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "DynamicAuraNovaActor.generated.h"

class USphereComponent;
class UAbilitySystemComponent;

UCLASS()
class GDB_API ADynamicAuraNovaActor : public AActor
{
	GENERATED_BODY()

public:
	ADynamicAuraNovaActor();

	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void InitializeAsAura(UAbilitySystemComponent* InSourceASC, const FDynamicAuraProfile& InAuraProfile, const TArray<FSourcePassBackRule>& InPassBackRules);
	void InitializeAsNova(UAbilitySystemComponent* InSourceASC, const FDynamicNovaProfile& InNovaProfile, const TArray<FSourcePassBackRule>& InPassBackRules);

protected:
	UFUNCTION()
	void OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnSphereEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spatial")
	USphereComponent* AreaSphere;

private:
	void TickAura(float DeltaTime);
	void TickNova(float DeltaTime);
	void ApplyPassBackToSource(EPassBackTriggerEvent EventType, float DamageAmount);

	bool bIsNovaMode = false;
	float ElapsedTime = 0.0f;
	float TickAccumulator = 0.0f;
	float CurrentRadius = 0.0f;

	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	FDynamicAuraProfile AuraProfile;
	FDynamicNovaProfile NovaProfile;
	TArray<FSourcePassBackRule> PassBackRules;

	// Tracks actors currently inside the Aura or already hit by the Nova wave
	UPROPERTY()
	TSet<TObjectPtr<AActor>> ActiveTargets;
};