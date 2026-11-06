#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "InputActionValue.h"
#include "DynamicPlayerController.generated.h"

UCLASS()
class GDB_API ADynamicPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void SetupInputComponent() override;

protected:
	virtual void BeginPlay() override;

	void HandleInputStarted(const FInputActionValue& Value, FGameplayTag ActionTag);
	void HandleInputTriggered(const FInputActionValue& Value, FGameplayTag ActionTag);
	void HandleInputCompleted(const FInputActionValue& Value, FGameplayTag ActionTag);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ExecuteQuickUseEquip(FGameplayTag ItemToEquipTag);

private:
	FGameplayTag ActiveOpenWheelTag;
};