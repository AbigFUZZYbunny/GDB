// DynamicPersistenceSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DynamicPersistenceSubsystem.generated.h"

class ADynamicCharacter;

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnPlayerPersistenceCompleteSignature, bool, bSuccess, const FString&, Message);

UCLASS()
class GDB_API UDynamicPersistenceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(EditDefaultsOnly, Category = "Persistence")
	FString BackendApiBaseUrl = TEXT("http://127.0.0.1:8080/api/characters");

	/**
	 * Dedicated Server Endpoint: Fetches JSON from backend and hydrates the character.
	 * ZERO client RPC transmission.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
	void ServerLoadPlayerFromDatabase(const FGuid& CharacterGuid, ADynamicCharacter* TargetCharacter, FOnPlayerPersistenceCompleteSignature OnComplete);

	/**
	 * Dedicated Server Endpoint: Serializes player state and posts directly to database.
	 * ZERO client RPC transmission.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Persistence")
	void ServerSavePlayerToDatabase(const ADynamicCharacter* InCharacter, FOnPlayerPersistenceCompleteSignature OnComplete);
};