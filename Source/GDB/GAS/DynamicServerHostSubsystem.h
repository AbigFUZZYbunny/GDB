// DynamicServerHostSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IHttpRequest.h"

// 1. Declare the native C++ delegate BEFORE the .generated.h include
//DECLARE_DELEGATE_OneParam(FOnSnapshotPushComplete, bool);

// 2. The generated.h MUST be the LAST header include in the file
#include "DynamicServerHostSubsystem.generated.h"

class ADynamicCharacter;

USTRUCT(BlueprintType)
struct FLocalServerSpoolConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Spooling")
    FString MapName = TEXT("DefaultWorld");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Spooling")
    int32 Port = 7777;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Spooling")
    int32 MaxPlayers = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Spooling")
    FString CloudAiMode = TEXT("heuristic");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Spooling")
    bool bEnableLogging = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Server Spooling")
    FString CustomCommandLineArguments = TEXT("");
};

UCLASS()
class GDB_API UDynamicServerHostSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Server Hosting")
    bool SpoolLocalDedicatedServer(const FLocalServerSpoolConfig& Config, int32& OutProcessId);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Server Hosting")
    bool IsLocalDedicatedServerRunning() const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Server Hosting")
    void TerminateLocalDedicatedServer();

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Server Hosting")
    void ConnectLocalPlayerToServer(int32 Port = 7777);

    // --- Tokenized Snapshot Management ---
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Handoff")
    FGuid StoreHandoffSnapshotWithToken(const FString& SnapshotJson);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Handoff")
    bool ConsumeHandoffSnapshotByToken(const FString& TokenStr, FString& OutSnapshotJson);

    UFUNCTION(BlueprintPure, BlueprintAuthorityOnly, Category = "Dynamic GAS|Handoff")
    bool HasHandoffToken(const FString& TokenStr) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Handoff")
    void InitiatePlayerServerHandoff(APlayerController* PlayerController, const FString& TargetServerIP, int32 TargetServerPort, const FString& TargetServerApiSecret);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Handoff")
    bool IngestPlayerHandoffPayload(const FGuid& PlayerGuid, const FString& JsonPayload);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Handoff")
    ADynamicCharacter* ClaimHandoffCharacter(const FGuid& PlayerGuid, const FTransform& SpawnTransform, AController* NewOwner);

    // Native C++ handoff dispatch (NO UFUNCTION - avoids UHT delegate reflection error)
    void PushSnapshotToTargetServer(const FString& TargetEndpoint, const FGuid& Token, const FString& JsonPayload, TFunction<void(bool)> OnComplete);

private:
    mutable FProcHandle ActiveServerProcessHandle;
    uint32 ActiveServerProcessId = 0;
    TMap<FGuid, FString> PendingHandoffSnapshots;

    void HandleHandoffIPCResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, TWeakObjectPtr<APlayerController> WeakPC, const FString& TravelURL);
};