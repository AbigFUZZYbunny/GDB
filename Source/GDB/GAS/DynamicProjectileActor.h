#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DynamicGASTypes.h"
#include "DynamicProjectileActor.generated.h"

class USphereComponent;
class UProjectileMovementComponent;
class UAbilitySystemComponent;

UCLASS()
class GDB_API ADynamicProjectileActor : public AActor
{
	GENERATED_BODY()

public:
	ADynamicProjectileActor();

	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server initializes projectile parameters from ability spec
	void InitializeProjectile(
		UAbilitySystemComponent* InSourceASC,
		const FDynamicPayloadProfile& InProfile,
		const TArray<FSourcePassBackRule>& InPassBackRules,
		float InInitialSpeed,
		float InPowerMultiplier,
		AActor* InHomingTarget = nullptr);

protected:
	UFUNCTION()
	void OnServerProjectileHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	USphereComponent* CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
	UProjectileMovementComponent* ProjectileMovement;

	UPROPERTY(Replicated)
	EProjectileGuidanceType GuidanceType = EProjectileGuidanceType::Straight;

	UPROPERTY(Replicated)
	float GuidanceTurnRate = 360.0f;

	UPROPERTY(Replicated)
	TWeakObjectPtr<AActor> HomingTarget;

private:
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	FDynamicPayloadProfile PayloadProfile;
	TArray<FSourcePassBackRule> PassBackRules;
	float PowerMultiplier = 1.0f;
};