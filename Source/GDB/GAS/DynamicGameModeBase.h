// DynamicGameModeBase.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DynamicGameModeBase.generated.h"

class ADynamicCharacter;

UCLASS()
class GDB_API ADynamicGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADynamicGameModeBase();

	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
	// Caches valid travel tokens per player controller during authentication
	TMap<TWeakObjectPtr<APlayerController>, FString> AuthenticatedPlayerTokens;
};