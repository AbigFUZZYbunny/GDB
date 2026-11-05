#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "DynamicGASTypes.h"
#include "Perception/AIPerceptionTypes.h"
#include "DynamicAIController.generated.h"

class UStateTreeComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UAISenseConfig_Damage;

UCLASS()
class GDB_API ADynamicAIController : public AAIController
{
	GENERATED_BODY()

public:
	ADynamicAIController(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic AI")
	UStateTreeComponent* StateTreeComponent;

	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(BlueprintCallable, Category = "Dynamic AI")
	void InitializeFromAgentDef(const FDynamicAIAgentDef& AgentDef);

protected:
	UFUNCTION()
	void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

private:
	UPROPERTY()
	UAISenseConfig_Sight* SightConfig;

	UPROPERTY()
	UAISenseConfig_Hearing* HearingConfig;

	UPROPERTY()
	UAISenseConfig_Damage* DamageConfig;
};