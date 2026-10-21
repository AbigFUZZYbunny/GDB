// DynamicServerHostSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "DynamicServerHostSubsystem.generated.h"

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

    // AI routing strategy passed via command line to the child process: "heuristic", "ollama", or "cloud"
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

    /**
     * Spools up a dedicated server binary from the local host client.
     * @param Config Startup configuration options including map, port, and AI pipeline.
     * @param OutProcessId Platform process ID of the spawned server.
     * @return True if the process successfully executed.
     */
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Server Hosting")
    bool SpoolLocalDedicatedServer(const FLocalServerSpoolConfig& Config, int32& OutProcessId);

    // Checks whether the spooled background server process is alive
    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Server Hosting")
    bool IsLocalDedicatedServerRunning() const;

    // Terminates the background server process if currently running
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Server Hosting")
    void TerminateLocalDedicatedServer();

    // Directs the local player controller to connect to the spooled server (127.0.0.1:Port)
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Server Hosting")
    void ConnectLocalPlayerToServer(int32 Port = 7777);

private:
    // Marked mutable so const BlueprintPure queries can pass non-const FProcHandle& references to FPlatformProcess
    mutable FProcHandle ActiveServerProcessHandle;
    uint32 ActiveServerProcessId = 0;
};