// DynamicCloudAISubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DynamicGASTypes.h"
#include "DynamicCloudAISubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCloudAIResponseReceivedSignature, const FGuid&, RequestGuid, const TArray<FCloudAICommandOutput>&, Commands);

UCLASS()
class GDB_API UDynamicCloudAISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Cloud AI")
	FOnCloudAIResponseReceivedSignature OnRemoteAIResponseReceived;

	// Dispatches an async request to external AI endpoints / Coin-miner mesh
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
	FGuid DispatchRemoteAIRequest(
		AActor* RequestingAgent,
		ECloudAIRequestType RequestType,
		const FString& SerializedAgentContextJson,
		float RequestTimeoutSeconds = 8.0f
	);

	// Endpoint called by HTTP response handlers or local mock loopbacks
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Cloud AI")
	void InjectRemoteAIResponse(const FGuid& RequestGuid, const FString& ResponseJson);

	// Checks if a specific agent currently has a pending request
	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Cloud AI")
	bool IsRequestPending(const FGuid& RequestGuid) const;

private:
	struct FPendingRemoteRequest
	{
		FGuid RequestGuid;
		TWeakObjectPtr<AActor> RequesterActor;
		ECloudAIRequestType RequestType;
		double DispatchWorldTime = 0.0;
		float TimeoutDuration = 8.0f;
	};

	TMap<FGuid, FPendingRemoteRequest> PendingRequests;
	FTimerHandle TimerHandle_RequestWatchdog;

	void CheckPendingRequestTimeouts();
};