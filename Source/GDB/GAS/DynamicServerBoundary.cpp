#include "DynamicServerBoundary.h"
#include "DynamicCharacter.h"
#include "DynamicDataSerializer.h"
#include "DynamicServerHostSubsystem.h"
#include "GameFramework/PlayerController.h"

ADynamicServerBoundary::ADynamicServerBoundary()
{
    PrimaryActorTick.bCanEverTick = false;
    BoundaryVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundaryVolume"));
    RootComponent = BoundaryVolume;
    BoundaryVolume->SetCollisionProfileName(TEXT("Trigger"));
    BoundaryVolume->OnComponentBeginOverlap.AddDynamic(this, &ADynamicServerBoundary::OnBoundaryOverlap);
}

void ADynamicServerBoundary::OnBoundaryOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!HasAuthority()) return;

    ADynamicCharacter* PlayerChar = Cast<ADynamicCharacter>(OtherActor);
    if (!PlayerChar || !PlayerChar->IsPlayerControlled()) return;

    APlayerController* PC = Cast<APlayerController>(PlayerChar->GetController());
    if (!PC) return;

    FString SerializedSnapshot;
    if (UDynamicDataSerializer::SerializePlayerToSnapshotJson(PlayerChar, SerializedSnapshot))
    {
        UDynamicServerHostSubsystem* HostSubsystem = GetWorld()->GetGameInstance()->GetSubsystem<UDynamicServerHostSubsystem>();
        if (HostSubsystem)
        {
            const FGuid TravelToken = FGuid::NewGuid();
            
            // Push the snapshot to the target server's admin API
            HostSubsystem->PushSnapshotToTargetServer(TargetServerEndpoint, TravelToken, SerializedSnapshot, [PC, this, TravelToken](bool bSuccess)
            {
                if (bSuccess && PC)
                {
                    // Command the client to disconnect and reconnect to the new server instance
                    const FString TravelURL = FString::Printf(TEXT("%s?Token=%s"), *TargetServerEndpoint, *TravelToken.ToString());
                    PC->ClientTravel(TravelURL, TRAVEL_Absolute);
                }
            });
        }
    }
}