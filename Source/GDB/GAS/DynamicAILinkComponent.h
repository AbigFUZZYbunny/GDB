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

	// Server requests the client to process an LLM task
	UFUNCTION(Client, Reliable)
	void Client_AssignAITask(const FGuid& TaskId, const FString& Prompt, const FString& Model, const FString& Endpoint, const FString& ApiKey);

	// Client streams delta tokens back to the server in real-time
	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_SubmitTokenChunk(const FGuid& TaskId, const FString& TokenChunk);

	// Client submits the final completed JSON payload for consensus verification
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SubmitFinalResult(const FGuid& TaskId, const FString& FinalJsonResult);

private:
	void OnStreamProgress(FHttpRequestPtr Request, int32 BytesSent, int32 BytesReceived);
	void OnStreamComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);

	FGuid ActiveTaskId;
	int32 LastParsedSseIndex = 0;
};