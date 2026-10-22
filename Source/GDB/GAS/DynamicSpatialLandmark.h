// DynamicSpatialLandmark.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSpatialLandmark.generated.h"

class USphereComponent;

/**
 * Concrete world anchor representing a village building, workplace, tavern, home, or point of interest.
 * Queriable by EQS and AI Behavior Trees via dynamic gameplay role tags.
 */
UCLASS()
class GDB_API ADynamicSpatialLandmark : public AActor
{
	GENERATED_BODY()

public:
	ADynamicSpatialLandmark();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Landmark")
	FGameplayTag LandmarkTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Dynamic GAS|Landmark")
	FGameplayTagContainer LandmarkRoleTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Landmark")
	FDynamicLocalizationProfile Localization;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Landmark")
	float ArrivalRadius = 200.0f;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Landmark")
	bool HasRoleTag(const FGameplayTag& RoleTag) const;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Landmark")
	bool MatchesAnyRoleTag(const FGameplayTagContainer& QueryRoleTags) const;

	UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Landmark")
	FVector GetArrivalLocation() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> ArrivalProximitySphere;
};