// DynamicCharacter.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "MoverComponent.h"
#include "PhysicsControlComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DynamicCharacter.generated.h"

class UAbilitySystemComponent;
class UDynamicAttributeSet;
class UDynamicGASManagerComponent;

UCLASS()
class GDB_API ADynamicCharacter : public APawn, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ADynamicCharacter(const FObjectInitializer& ObjectInitializer);

	// --- Native GAS Accessor Override ---
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
	virtual void BeginPlay() override;
	
	// Handles server-side possession allocation routines
	virtual void PossessedBy(AController* NewController) override;
	
	// Handles client-side proxy binding synchronizations
	virtual void OnRep_PlayerState() override;

	// --- Core Locomotion Sizing & Mesh Structures ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCapsuleComponent* CapsuleComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	UMoverComponent* MoverComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Physics", meta = (AllowPrivateAccess = "true"))
	UPhysicsControlComponent* PhysicsControlComponent;

	// --- Dynamic Data-Driven GAS Layers ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Abilities")
	const UDynamicAttributeSet* AttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UDynamicGASManagerComponent* DynamicGASManager;

	// --- Physical Ragdoll States ---
	UFUNCTION(BlueprintCallable, Category = "Abilities|Ragdoll")
	void StartRagdoll();

	UFUNCTION(BlueprintCallable, Category = "Abilities|Ragdoll")
	void RecoverFromRagdoll();

private:
	void InitializeAbilitySystem();

	bool bIsRagdolling;
};