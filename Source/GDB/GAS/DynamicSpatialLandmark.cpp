// DynamicSpatialLandmark.cpp
#include "DynamicSpatialLandmark.h"
#include "DynamicGameState.h"
#include "Components/SphereComponent.h"
#include "Net/UnrealNetwork.h"

ADynamicSpatialLandmark::ADynamicSpatialLandmark()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    ArrivalProximitySphere = CreateDefaultSubobject<USphereComponent>(TEXT("ArrivalProximitySphere"));
    ArrivalProximitySphere->SetupAttachment(RootComponent);
    ArrivalProximitySphere->SetSphereRadius(ArrivalRadius);
    ArrivalProximitySphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    ArrivalProximitySphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ADynamicSpatialLandmark::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        if (UWorld* World = GetWorld())
        {
            if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
            {
                FGameplayTagContainer CombinedTags = LandmarkRoleTags;
                if (LandmarkTag.IsValid())
                {
                    CombinedTags.AddTag(LandmarkTag);
                }
                GS->RegisterSpatialRoleActor(this, CombinedTags);
            }
        }
    }
}

void ADynamicSpatialLandmark::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        if (UWorld* World = GetWorld())
        {
            if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
            {
                GS->UnregisterSpatialRoleActor(this);
            }
        }
    }

    Super::EndPlay(EndPlayReason);
}

void ADynamicSpatialLandmark::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION(ADynamicSpatialLandmark, LandmarkTag, COND_InitialOnly);
    DOREPLIFETIME(ADynamicSpatialLandmark, LandmarkRoleTags);
}

bool ADynamicSpatialLandmark::HasRoleTag(const FGameplayTag& RoleTag) const
{
    if (!RoleTag.IsValid()) return false;
    return (LandmarkTag.MatchesTag(RoleTag) || LandmarkRoleTags.HasTag(RoleTag));
}

bool ADynamicSpatialLandmark::MatchesAnyRoleTag(const FGameplayTagContainer& QueryRoleTags) const
{
    if (QueryRoleTags.Num() == 0) return false;
    return LandmarkTag.MatchesAny(QueryRoleTags) || LandmarkRoleTags.HasAny(QueryRoleTags);
}

FVector ADynamicSpatialLandmark::GetArrivalLocation() const
{
    return GetActorLocation();
}