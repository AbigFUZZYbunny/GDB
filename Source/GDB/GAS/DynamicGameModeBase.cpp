// DynamicGameModeBase.cpp
#include "DynamicGameModeBase.h"
#include "DynamicServerHostSubsystem.h"
#include "DynamicDataSerializer.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "Kismet/GameplayStatics.h"

ADynamicGameModeBase::ADynamicGameModeBase()
{
    DefaultPawnClass = ADynamicCharacter::StaticClass();
    GameStateClass = ADynamicGameState::StaticClass();
}

void ADynamicGameModeBase::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (!ErrorMessage.IsEmpty()) return;

    const FString TravelToken = UGameplayStatics::ParseOption(Options, TEXT("Token"));
    if (!TravelToken.IsEmpty())
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            if (UDynamicServerHostSubsystem* HostSubsystem = GI->GetSubsystem<UDynamicServerHostSubsystem>())
            {
                if (!HostSubsystem->HasHandoffToken(TravelToken))
                {
                    ErrorMessage = TEXT("ServerHandoff: Invalid or expired TravelToken.");
                }
            }
        }
    }
}

FString ADynamicGameModeBase::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
    const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
    if (!Error.IsEmpty()) return Error;

    const FString TravelToken = UGameplayStatics::ParseOption(Options, TEXT("Token"));
    if (!TravelToken.IsEmpty() && NewPlayerController)
    {
        AuthenticatedPlayerTokens.Add(NewPlayerController, TravelToken);
    }

    return Error;
}

void ADynamicGameModeBase::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    if (!NewPlayer) return;

    FString TravelToken;
    if (AuthenticatedPlayerTokens.RemoveAndCopyValue(NewPlayer, TravelToken))
    {
        if (UGameInstance* GI = GetGameInstance())
        {
            if (UDynamicServerHostSubsystem* HostSubsystem = GI->GetSubsystem<UDynamicServerHostSubsystem>())
            {
                FString SnapshotJson;
                if (HostSubsystem->ConsumeHandoffSnapshotByToken(TravelToken, SnapshotJson))
                {
                    if (ADynamicCharacter* CharPawn = Cast<ADynamicCharacter>(NewPlayer->GetPawn()))
                    {
                        // Asynchronous hydration: JSON string-to-struct executes on ThreadPool
                        UDynamicDataSerializer::DeserializePlayerFromSnapshotJsonAsyncNative(
                            CharPawn, 
                            SnapshotJson, 
                            [WeakPC = TWeakObjectPtr<APlayerController>(NewPlayer)](bool bSuccess)
                            {
                                if (!bSuccess)
                                {
                                    UE_LOG(LogTemp, Warning, TEXT("ADynamicGameModeBase: Async hydration failed for connecting player."));
                                }
                            }
                        );
                    }
                }
            }
        }
    }
}