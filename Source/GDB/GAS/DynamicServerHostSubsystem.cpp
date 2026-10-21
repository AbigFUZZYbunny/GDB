// DynamicServerHostSubsystem.cpp
#include "DynamicServerHostSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

void UDynamicServerHostSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicServerHostSubsystem::Deinitialize()
{
    TerminateLocalDedicatedServer();
    Super::Deinitialize();
}

bool UDynamicServerHostSubsystem::SpoolLocalDedicatedServer(const FLocalServerSpoolConfig& Config, int32& OutProcessId)
{
    OutProcessId = 0;

    if (IsLocalDedicatedServerRunning())
    {
        TerminateLocalDedicatedServer();
    }

#if PLATFORM_WINDOWS
    FString ServerExecutable = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/GDBServer.exe"));
    if (!FPaths::FileExists(ServerExecutable))
    {
        ServerExecutable = FPlatformProcess::GenerateApplicationPath(FApp::GetName(), EBuildConfiguration::Development);
    }
#else
    FString ServerExecutable = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Linux/GDBServer"));
#endif

    if (!FPaths::FileExists(ServerExecutable))
    {
        UE_LOG(LogTemp, Error, TEXT("UDynamicServerHostSubsystem: Server executable not found at '%s'."), *ServerExecutable);
        return false;
    }

    FString LaunchArguments = FString::Printf(TEXT("%s?listen -port=%d -MaxPlayers=%d -cloudai=%s %s"),
        *Config.MapName,
        Config.Port,
        Config.MaxPlayers,
        *Config.CloudAiMode,
        Config.bEnableLogging ? TEXT("-log") : TEXT(""));

    if (!ServerExecutable.Contains(TEXT("GDBServer")))
    {
        LaunchArguments.Append(TEXT(" -server"));
    }

    if (!Config.CustomCommandLineArguments.IsEmpty())
    {
        LaunchArguments.Append(FString::Printf(TEXT(" %s"), *Config.CustomCommandLineArguments));
    }

    uint32 ProcessId = 0;
    ActiveServerProcessHandle = FPlatformProcess::CreateProc(
        *ServerExecutable,
        *LaunchArguments,
        true,       // bLaunchDetached
        false,      // bLaunchHidden
        false,      // bLaunchReallyHidden
        &ProcessId,
        0,          // Priority
        *FPaths::ProjectDir(),
        nullptr,
        nullptr
    );

    if (ActiveServerProcessHandle.IsValid())
    {
        ActiveServerProcessId = ProcessId;
        OutProcessId = static_cast<int32>(ProcessId);
        return true;
    }

    return false;
}

bool UDynamicServerHostSubsystem::IsLocalDedicatedServerRunning() const
{
    if (!ActiveServerProcessHandle.IsValid())
    {
        return false;
    }

    // Resolves C2664: copy into a non-const local handle to pass to FPlatformProcess::IsProcRunning
    FProcHandle ProcHandleCopy = ActiveServerProcessHandle;
    const bool bRunning = FPlatformProcess::IsProcRunning(ProcHandleCopy);
    ActiveServerProcessHandle = ProcHandleCopy;
    return bRunning;
}

void UDynamicServerHostSubsystem::TerminateLocalDedicatedServer()
{
    if (ActiveServerProcessHandle.IsValid())
    {
        FProcHandle ProcHandleCopy = ActiveServerProcessHandle;
        if (FPlatformProcess::IsProcRunning(ProcHandleCopy))
        {
            FPlatformProcess::TerminateProc(ProcHandleCopy, true);
        }
        FPlatformProcess::CloseProc(ProcHandleCopy);
        ActiveServerProcessHandle.Reset();
        ActiveServerProcessId = 0;
    }
}

void UDynamicServerHostSubsystem::ConnectLocalPlayerToServer(int32 Port)
{
    if (UWorld* World = GetWorld())
    {
        APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
        if (PC)
        {
            const FString ConnectURL = FString::Printf(TEXT("127.0.0.1:%d"), Port);
            PC->ClientTravel(ConnectURL, TRAVEL_Absolute);
        }
    }
}