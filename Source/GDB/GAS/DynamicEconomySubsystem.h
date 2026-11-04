// DynamicEconomySubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicEconomySubsystem.generated.h"

UCLASS()
class GDB_API UDynamicEconomySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { return TStatId(); }

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Economy")
	void PostNPCWorkOrder(const FDynamicNpcWorkOrderRequest& Request);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Economy")
	bool FulfillWorkOrder(const FGuid& WorkOrderGuid, AActor* FulfillingPlayer);

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Economy")
	float CalculateCurrentMarketPrice(FGameplayTag MarketTag, FGameplayTag ItemArchetypeTag) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Economy")
	void RegisterMarketTransaction(FGameplayTag MarketTag, FGameplayTag ItemArchetypeTag, int32 QuantitySold, int32 QuantityBought);

private:
	float MacroSimulationTimer = 0.0f;
    
	UPROPERTY()
	TMap<FGameplayTag, FDynamicMarketRegionDef> ActiveRegionalMarkets;

	UPROPERTY()
	TMap<FGuid, FDynamicNpcWorkOrderRequest> ActiveWorkOrders;

	// Recalculates exact supply/demand based on actual containers and living population
	void SimulateMacroEconomy(float DeltaTime);
};