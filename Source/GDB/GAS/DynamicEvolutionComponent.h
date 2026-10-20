// DynamicEvolutionComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "DynamicEvolutionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEvolutionStageAdvancedSignature, FGameplayTag, NewStageTag, float, CurrentProgress, float, CurrentScale);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicEvolutionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicEvolutionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Evolution")
	FOnEvolutionStageAdvancedSignature OnEvolutionStageAdvanced;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
	FGameplayTag EvolutionProfileTag;

	UPROPERTY(ReplicatedUsing = OnRep_EvolutionData, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
	float CurrentProgressValue = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_EvolutionData, BlueprintReadOnly, Category = "Dynamic GAS|Evolution")
	FGameplayTag CurrentStageTag;

	UFUNCTION()
	void OnRep_EvolutionData();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Evolution")
	void ServerAddProgress(float DeltaAmount);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Evolution")
	FDynamicTagFloatMapWrapper LocalEnvironmentOffsets;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Evolution")
	float CalculateEnvironmentalProgressMultiplier() const;

private:
	void EvaluateEvolution(float NewProgress);
};