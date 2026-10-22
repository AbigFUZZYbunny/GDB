// DynamicCloudAISubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicCloudAISubsystem.generated.h"

class ADynamicCharacter;
class FJsonObject;

USTRUCT(BlueprintType)
struct FCloudAICommand
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag CommandActionTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FGameplayTag TargetEntityOrPlaceRoleTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    FString UtteranceText;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    float PriorityWeight = 0.0f;
};

USTRUCT(BlueprintType)
struct FCloudAIDirectiveResponse
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI")
    TArray<FCloudAICommand> Commands;
};

// Standard C++ Delegate supporting BindLambda
DECLARE_DELEGATE_TwoParams(FOnCloudAIDirectiveReceived, bool, const FCloudAIDirectiveResponse&);
DECLARE_DELEGATE_OneParam(FOnCloudAIResponseDirectCallback, const TArray<FCloudAICommandOutput>&);

UCLASS()
class GDB_API UDynamicCloudAISubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    ECloudAIExecutionBackend ActiveBackend = ECloudAIExecutionBackend::LocalRuleBasedFallback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    FString EndpointUrl = TEXT("http://127.0.0.1:8000/v1/chat/completions");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    FString ApiKey = TEXT("");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cloud AI|Configuration")
    FString ModelName = TEXT("default-model");

    void RequestCloudAIDirective(ADynamicCharacter* Character, const FString& ContextPrompt, FOnCloudAIDirectiveReceived OnCompleted);

    FGuid DispatchRemoteAIRequestNative(
        APawn* AgentPawn,
        ECloudAIRequestType RequestType,
        const FString& ContextJsonPrompt,
        float TimeoutSeconds,
        FOnCloudAIResponseDirectCallback DirectCallback
    );

    UFUNCTION(BlueprintCallable, Category = "Cloud AI")
    void CancelRemoteAIRequest(const FGuid& RequestGuid);

    UFUNCTION(BlueprintPure, Category = "Cloud AI")
    static FString SanitizeMarkdownJson(const FString& RawResponse);

    TSharedPtr<FJsonObject> GenerateCloudAIJsonSchema() const;

    void ExecuteRuleBasedFallback(ADynamicCharacter* Character, FCloudAIDirectiveResponse& OutFallbackResponse);

private:
    TMap<FGuid, FOnCloudAIResponseDirectCallback> ActiveDirectCallbacks;
};