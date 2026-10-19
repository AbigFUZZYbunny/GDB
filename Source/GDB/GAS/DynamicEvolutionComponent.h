#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "DynamicEvolutionTypes.h"
#include "DynamicEvolutionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEvolutionStageAdvancedSignature, FGameplayTag, NewStageTag, float, CurrentProgress, float, CurrentScale);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicEvolutionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicEvolutionComponent();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Evolution")
	FOnEvolutionStageAdvancedSignature OnEvolutionStageAdvanced;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
	FGameplayTag EvolutionProfileTag; // Links to master JSON registry

	UPROPERTY(ReplicatedUsing = OnRep_EvolutionData, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
	float CurrentProgressValue = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_EvolutionData, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
	FGameplayTag CurrentStageTag;

	UFUNCTION() void OnRep_EvolutionData();

	// Advances evolution progress (driven by time for fruit/trees, or XP/usage for gear)
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Evolution")
	void ServerAddProgress(float DeltaAmount);
	
	// Local offsets applied by containers, iceboxes, campfires, or refrigeration spells
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
	FDynamicTagFloatMapWrapper LocalEnvironmentOffsets;

	// Calculates the effective multiplier from local temperature, wetness, and sunlight
	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Evolution")
	float CalculateEnvironmentalProgressMultiplier() const;

private:
	void EvaluateEvolution(float NewProgress);
};