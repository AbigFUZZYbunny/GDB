// DynamicPersistenceSubsystem.cpp
#include "DynamicPersistenceSubsystem.h"
#include "DynamicDataSerializer.h"
#include "DynamicCharacter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

void UDynamicPersistenceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDynamicPersistenceSubsystem::Deinitialize()
{
    Super::Deinitialize();
}

void UDynamicPersistenceSubsystem::ServerLoadPlayerFromDatabase(
    const FGuid& CharacterGuid, 
    ADynamicCharacter* TargetCharacter, 
    FOnPlayerPersistenceCompleteSignature OnComplete)
{
    if (!TargetCharacter || !TargetCharacter->HasAuthority() || !CharacterGuid.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("Invalid target character or GUID on server"));
        return;
    }

    const FString RequestUrl = FString::Printf(TEXT("%s/%s"), *BackendApiBaseUrl, *CharacterGuid.ToString());
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();

    HttpRequest->SetURL(RequestUrl);
    HttpRequest->SetVerb(TEXT("GET"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    TWeakObjectPtr<ADynamicCharacter> WeakChar(TargetCharacter);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [WeakChar, OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
    {
        if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
        {
            const FString ErrMsg = Response.IsValid() ? FString::Printf(TEXT("HTTP error %d"), Response->GetResponseCode()) : TEXT("Connection failed");
            OnComplete.ExecuteIfBound(false, ErrMsg);
            return;
        }

        ADynamicCharacter* Char = WeakChar.Get();
        if (!Char || !Char->HasAuthority())
        {
            OnComplete.ExecuteIfBound(false, TEXT("Target character was destroyed before response arrived"));
            return;
        }

        // Hydrate player authoritatively on server; replication handles client proxies
        const bool bHydrated = UDynamicDataSerializer::DeserializePlayerFromSnapshotJson(Char, Response->GetContentAsString());
        OnComplete.ExecuteIfBound(bHydrated, bHydrated ? TEXT("Loaded successfully") : TEXT("Failed to parse player snapshot"));
    });

    HttpRequest->ProcessRequest();
}

void UDynamicPersistenceSubsystem::ServerSavePlayerToDatabase(
    const ADynamicCharacter* InCharacter, 
    FOnPlayerPersistenceCompleteSignature OnComplete)
{
    if (!InCharacter || !InCharacter->HasAuthority())
    {
        OnComplete.ExecuteIfBound(false, TEXT("Character is null or not authoritative"));
        return;
    }

    FString JsonPayload;
    if (!UDynamicDataSerializer::SerializePlayerToSnapshotJson(InCharacter, JsonPayload))
    {
        OnComplete.ExecuteIfBound(false, TEXT("Failed to serialize player snapshot"));
        return;
    }

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(BackendApiBaseUrl);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    HttpRequest->SetContentAsString(JsonPayload);

    HttpRequest->OnProcessRequestComplete().BindLambda(
        [OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully)
    {
        const bool bSuccess = bConnectedSuccessfully && Response.IsValid() && (Response->GetResponseCode() == 200 || Response->GetResponseCode() == 201);
        const FString Msg = bSuccess ? TEXT("Saved successfully") : TEXT("Backend save rejected");
        OnComplete.ExecuteIfBound(bSuccess, Msg);
    });

    HttpRequest->ProcessRequest();
}