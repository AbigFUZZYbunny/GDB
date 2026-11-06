#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DynamicGASTypes.h"
#include "InputMappingContext.h"
#include "DynamicInputSubsystem.generated.h"

class UInputAction;

UCLASS()
class GDB_API UDynamicInputSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "Dynamic Input")
	void LoadInputDefinitions();

	UFUNCTION(BlueprintCallable, Category = "Dynamic Input")
	void RebindKey(FGameplayTag ActionTag, const FString& NewKeyName, int32 BindingIndex = 0);

	const TMap<FGameplayTag, UInputAction*>& GetGeneratedActions() const { return GeneratedActions; }
	UInputMappingContext* GetDynamicMappingContext() const { return DynamicMappingContext; }

private:
	void BuildMappingContext();
	UInputAction* GetOrCreateChordAction(const FString& ChordKeyName);

	UPROPERTY()
	TMap<FGameplayTag, FDynamicInputActionDef> InputMasterRegistry;

	UPROPERTY()
	TMap<FGameplayTag, UInputAction*> GeneratedActions;

	UPROPERTY()
	TMap<FString, UInputAction*> ChordActionsCache;

	UPROPERTY()
	UInputMappingContext* DynamicMappingContext = nullptr;
};