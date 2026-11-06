// DynamicAIController.cpp
#include "DynamicAIController.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicSocialComponent.h"
#include "StateTree.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Damage.h"
#include "Components/StateTreeComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

ADynamicAIController::ADynamicAIController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("PerceptionComponent"));
    StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("StateTreeComponent"));

    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
    DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
}

void ADynamicAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    if (PerceptionComponent)
    {
        PerceptionComponent->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ADynamicAIController::OnTargetPerceptionUpdated);
    }
}

void ADynamicAIController::InitializeFromAgentDef(const FDynamicAIAgentDef& AgentDef)
{
    if (!PerceptionComponent) return;

    // 1. Dynamic Perception Configuration
    SightConfig->SightRadius = AgentDef.PerceptionConfig.SightRadius;
    SightConfig->LoseSightRadius = AgentDef.PerceptionConfig.LoseSightRadius;
    SightConfig->PeripheralVisionAngleDegrees = AgentDef.PerceptionConfig.PeripheralVisionAngleDegrees;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

    HearingConfig->HearingRange = AgentDef.PerceptionConfig.HearingRange;
    HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
    HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
    HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

    PerceptionComponent->ConfigureSense(*SightConfig);
    PerceptionComponent->ConfigureSense(*HearingConfig);
    if (AgentDef.PerceptionConfig.bEnableDamageSense)
    {
        PerceptionComponent->ConfigureSense(*DamageConfig);
    }
    
    // Pass TSubclassOf<UAISense> instead of FAISenseID
    PerceptionComponent->SetDominantSense(UAISense_Sight::StaticClass());

    // 2. Dynamic State Tree Initialization
    if (!AgentDef.StateTreeAsset.IsNull())
    {
        FSoftObjectPath AssetPath = AgentDef.StateTreeAsset.ToSoftObjectPath();
        TWeakObjectPtr<ADynamicAIController> WeakThis(this);
        
        UAssetManager::GetStreamableManager().RequestAsyncLoad(AssetPath, FStreamableDelegate::CreateLambda([WeakThis, AssetPath]() {
            if (ADynamicAIController* StrongThis = WeakThis.Get())
            {
                if (UStateTree* LoadedTree = Cast<UStateTree>(AssetPath.ResolveObject()))
                {
                    StrongThis->StateTreeComponent->SetStateTree(LoadedTree);
                    StrongThis->StateTreeComponent->StartLogic();
                }
            }
        }));
    }
}

void ADynamicAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    if (!GetPawn()) return;

    if (UDynamicSocialComponent* Social = GetPawn()->FindComponentByClass<UDynamicSocialComponent>())
    {
        if (Stimulus.WasSuccessfullySensed())
        {
            FGameplayTag SenseTag = FGameplayTag::RequestGameplayTag(TEXT("Sense.Vision"), false);
            if (Stimulus.Type == HearingConfig->GetSenseID()) SenseTag = FGameplayTag::RequestGameplayTag(TEXT("Sense.Hearing"), false);
            else if (Stimulus.Type == DamageConfig->GetSenseID()) SenseTag = FGameplayTag::RequestGameplayTag(TEXT("Sense.Damage"), false);

            TMap<FGameplayTag, float> Signatures;
            Signatures.Add(SenseTag, 100.0f);
            Social->ServerProcessCrimeSensoryEvent(FGameplayTag::EmptyTag, Actor, nullptr, FGuid(), TArray<FGuid>(), Signatures);
        }
    }
}