#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/BoxComponent.h"
#include "DynamicServerBoundary.generated.h"

UCLASS()
class GDB_API ADynamicServerBoundary : public AActor
{
	GENERATED_BODY()
public:
	ADynamicServerBoundary();

	// The IP and Port of the destination server (e.g., "127.0.0.1:7778" or a matchmaking endpoint)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Boundary Handoff")
	FString TargetServerEndpoint;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boundary Handoff")
	UBoxComponent* BoundaryVolume;

	UFUNCTION()
	void OnBoundaryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};