// EnvQueryTest_LandmarkRole.h
#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "GameplayTagContainer.h"
#include "EnvQueryTest_LandmarkRole.generated.h"

/**
 * EQS Test that filters candidate actors (ADynamicSpatialLandmark, ADynamicPhysicsActor)
 * by verifying they possess the specified role tag (either static or dynamic via Blackboard).
 */
UCLASS()
class GDB_API UEnvQueryTest_LandmarkRole : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnvQueryTest_LandmarkRole();

	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;

	UPROPERTY(EditDefaultsOnly, Category = "Landmark Role")
	FGameplayTag RoleTagToMatch;

	UPROPERTY(EditDefaultsOnly, Category = "Landmark Role")
	FName RoleTagBlackboardKey = NAME_None;
};