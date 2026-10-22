// DynamicServerHostSubsystem.cpp
#include "DynamicServerHostSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "DynamicDataSerializer.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "Runtime/Online/HTTP/Public/HttpModule.h"
#include "Runtime/Online/HTTP/Public/Interfaces/IHttpResponse.h"

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

void UDynamicServerHostSubsystem::InitiatePlayerServerHandoff(APlayerController* PlayerController, const FString& TargetServerIP, int32 TargetServerPort, const FString& TargetServerApiSecret)
{
    if (!PlayerController) return;

    ADynamicCharacter* CharPawn = Cast<ADynamicCharacter>(PlayerController->GetPawn());
    if (!CharPawn) return;

    FString SerializedPayload;
    if (!UDynamicDataSerializer::SerializePlayerToSnapshotJson(CharPawn, SerializedPayload))
    {
        UE_LOG(LogTemp, Error, TEXT("ServerHandoff: Failed to serialize character."));
        return;
    }

    FGuid PlayerGuid = FGuid::NewGuid();
    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SerializedPayload);
    if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
    {
        FString GuidStr;
        if (RootObj->TryGetStringField(TEXT("CharacterGuid"), GuidStr))
        {
            FGuid::Parse(GuidStr, PlayerGuid);
        }
    }

    const FString EndpointUrl = FString::Printf(TEXT("http://%s:%d/api/handoff/ingest"), *TargetServerIP, TargetServerPort);
    
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(EndpointUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetHeader(TEXT("X-Handoff-Secret"), TargetServerApiSecret);
    HttpRequest->SetHeader(TEXT("X-Player-Guid"), PlayerGuid.ToString());
    HttpRequest->SetContentAsString(SerializedPayload);

    const FString TravelURL = FString::Printf(TEXT("%s:%d?Token=%s"), *TargetServerIP, TargetServerPort, *PlayerGuid.ToString());
    TWeakObjectPtr<APlayerController> WeakPC(PlayerController);
    TWeakObjectPtr<UDynamicServerHostSubsystem> WeakSubsystem(this);

    // Fix: Use BindLambda to cleanly pass context arguments to HandleHandoffIPCResponse
    HttpRequest->OnProcessRequestComplete().BindLambda([WeakSubsystem, WeakPC, TravelURL](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
    {
        if (UDynamicServerHostSubsystem* Subsystem = WeakSubsystem.Get())
        {
            Subsystem->HandleHandoffIPCResponse(Request, Response, bConnectedSuccessfully, WeakPC, TravelURL);
        }
    });

    HttpRequest->ProcessRequest();
}

void UDynamicServerHostSubsystem::HandleHandoffIPCResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully, TWeakObjectPtr<APlayerController> WeakPC, const FString& TravelURL)
{
    APlayerController* PC = WeakPC.Get();
    if (!PC) return;

    if (bConnectedSuccessfully && Response.IsValid() && EHttpResponseCodes::IsOk(Response->GetResponseCode()))
    {
        // Step 4: Client Redirect (Seamless Server Travel execution)
        UE_LOG(LogTemp, Log, TEXT("ServerHandoff: Target chunk server ingested payload. Redirecting client to %s"), *TravelURL);
        PC->ClientTravel(TravelURL, TRAVEL_Absolute);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ServerHandoff: Failed to transfer authority to target chunk. Aborting handoff."));
        // Handle boundary bounce-back logic here (e.g., apply a reverse impulse)
    }
}

bool UDynamicServerHostSubsystem::IngestPlayerHandoffPayload(const FGuid& PlayerGuid, const FString& JsonPayload)
{
    if (!PlayerGuid.IsValid() || JsonPayload.IsEmpty()) return false;

    // Cache the snapshot awaiting the client socket connection
    PendingHandoffSnapshots.Add(PlayerGuid, JsonPayload);
    return true;
}

ADynamicCharacter* UDynamicServerHostSubsystem::ClaimHandoffCharacter(const FGuid& PlayerGuid, const FTransform& SpawnTransform, AController* NewOwner)
{
    FString Payload;
    if (!PendingHandoffSnapshots.RemoveAndCopyValue(PlayerGuid, Payload))
    {
        return nullptr;
    }

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS || !NewOwner) return nullptr;

    // Fast-parse just the TemplateTag to acquire the right skeletal mesh and morphs
    FGameplayTag TemplateTag;
    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Payload);
    if (FJsonSerializer::Deserialize(Reader, RootObj) && RootObj.IsValid())
    {
        FString TagStr;
        if (RootObj->TryGetStringField(TEXT("CharacterTemplateTag"), TagStr))
        {
            TemplateTag = FGameplayTag::RequestGameplayTag(FName(*TagStr), false);
        }
    }

    // Fix: Pass NewOwner (AController*) and NewOwner->GetPawn() (APawn*) correctly without invalid Cast<>
    APawn* InstigatorPawn = NewOwner->GetPawn();
    ADynamicCharacter* ClaimedCharacter = GS->AcquireCharacter(TemplateTag, SpawnTransform, NewOwner, InstigatorPawn);
    
    if (ClaimedCharacter)
    {
        if (UDynamicDataSerializer::DeserializePlayerFromSnapshotJson(ClaimedCharacter, Payload))
        {
            NewOwner->Possess(ClaimedCharacter);
            return ClaimedCharacter;
        }
    }

    return nullptr;
}

FGuid UDynamicServerHostSubsystem::StoreHandoffSnapshotWithToken(const FString& SnapshotJson)
{
    const FGuid NewToken = FGuid::NewGuid();
    PendingHandoffSnapshots.Add(NewToken, SnapshotJson);

    // Save to disk for cross-process local server spooling
    const FString HandoffDir = FPaths::ProjectSavedDir() / TEXT("Handoffs");
    IFileManager::Get().MakeDirectory(*HandoffDir, true);
    const FString FilePath = HandoffDir / FString::Printf(TEXT("%s.json"), *NewToken.ToString());
    FFileHelper::SaveStringToFile(SnapshotJson, *FilePath);

    return NewToken;
}

bool UDynamicServerHostSubsystem::ConsumeHandoffSnapshotByToken(const FString& TokenStr, FString& OutSnapshotJson)
{
    FGuid TokenGuid;
    if (!FGuid::Parse(TokenStr, TokenGuid))
    {
        return false;
    }

    // 1. Check in-memory cache (Remote MMO HTTP Ingest path)
    if (PendingHandoffSnapshots.RemoveAndCopyValue(TokenGuid, OutSnapshotJson))
    {
        return true;
    }

    // 2. Fall back to local file cache (Cross-process local server spool path)
    const FString FilePath = FPaths::ProjectSavedDir() / TEXT("Handoffs") / FString::Printf(TEXT("%s.json"), *TokenStr);
    if (FPaths::FileExists(FilePath))
    {
        if (FFileHelper::LoadFileToString(OutSnapshotJson, *FilePath))
        {
            IFileManager::Get().Delete(*FilePath); // Delete file once consumed
            return true;
        }
    }

    return false;
}

bool UDynamicServerHostSubsystem::HasHandoffToken(const FString& TokenStr) const
{
    FGuid TokenGuid;
    if (!FGuid::Parse(TokenStr, TokenGuid))
    {
        return false;
    }

    if (PendingHandoffSnapshots.Contains(TokenGuid))
    {
        return true;
    }

    const FString FilePath = FPaths::ProjectSavedDir() / TEXT("Handoffs") / FString::Printf(TEXT("%s.json"), *TokenStr);
    return FPaths::FileExists(FilePath);
}

void UDynamicServerHostSubsystem::PushSnapshotToTargetServer(const FString& TargetEndpoint, const FGuid& Token, const FString& JsonPayload, TFunction<void(bool)> OnComplete)
{
    // 1. Store snapshot locally for cross-process local server spooling
    StoreHandoffSnapshotWithToken(JsonPayload);

    // 2. Transmit via HTTP if pointing to an external dedicated server endpoint
    if (!TargetEndpoint.IsEmpty() && TargetEndpoint.Contains(TEXT(":")))
    {
        const FString Url = FString::Printf(TEXT("http://%s/api/handoff/ingest"), *TargetEndpoint);
        TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
        HttpRequest->SetURL(Url);
        HttpRequest->SetVerb(TEXT("POST"));
        HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
        HttpRequest->SetHeader(TEXT("X-Handoff-Token"), Token.ToString());
        HttpRequest->SetContentAsString(JsonPayload);

        HttpRequest->OnProcessRequestComplete().BindLambda([OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
        {
            const bool bSuccess = bConnectedSuccessfully && Response.IsValid() && (Response->GetResponseCode() >= 200 && Response->GetResponseCode() < 300);
            if (OnComplete)
            {
                OnComplete(bSuccess);
            }
        });
        HttpRequest->ProcessRequest();
        return;
    }

    if (OnComplete)
    {
        OnComplete(true);
    }
}