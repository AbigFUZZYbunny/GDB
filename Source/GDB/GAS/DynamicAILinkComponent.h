// DynamicAILinkComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicGASTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "DynamicAILinkComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicAILinkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicAILinkComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(BlueprintReadOnly, Category = "Dynamic GAS|Miner Node")
	FMinerNodeStats NodeStats;

	UFUNCTION(Client, Reliable)
	void Client_AssignAITask(const FGuid& TaskId, const FString& Prompt, const FString& Model, const FString& Endpoint, const FString& ApiKey);

	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_SubmitTokenChunk(const FGuid& TaskId, const FString& TokenChunk);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SubmitFinalResult(const FGuid& TaskId, const FString& FinalJsonResult);

private:
	void OnStreamProgress64(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void OnStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	FGuid ActiveTaskId;
	int32 LastParsedSseIndex = 0;

	// PREVENTS CRASHES: Thread-safe pointer allows safe cancellation if component is destroyed mid-stream
	TSharedPtr<class IHttpRequest, ESPMode::ThreadSafe> ActiveHttpRequest;
};