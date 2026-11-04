// DynamicEconomySubsystem.cpp
#include "DynamicEconomySubsystem.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicContainerComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicQuestComponent.h"

void UDynamicEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection) 
{ 
    Super::Initialize(Collection);
    
    // Copy the pristine JSON definitions from GameState at startup into our mutable live economy state
    if (ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr)
    {
        ActiveRegionalMarkets = GS->MarketMasterRegistry;
    }
}
void UDynamicEconomySubsystem::Deinitialize() { ActiveRegionalMarkets.Empty(); ActiveWorkOrders.Empty(); Super::Deinitialize(); }

void UDynamicEconomySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (GetWorld() && GetWorld()->GetNetMode() != NM_Client)
    {
        MacroSimulationTimer += DeltaTime;
        if (MacroSimulationTimer >= 60.0f) // Recalculate inflation and demands every 60 seconds
        {
            MacroSimulationTimer = 0.0f;
            SimulateMacroEconomy(60.0f);
        }
    }
}

float UDynamicEconomySubsystem::CalculateCurrentMarketPrice(FGameplayTag MarketTag, FGameplayTag ItemArchetypeTag) const
{
    if (const FDynamicMarketRegionDef* Market = ActiveRegionalMarkets.Find(MarketTag))
    {
        for (const FDynamicMarketCommodity& Commodity : Market->BaselineCommodities)
        {
            if (Commodity.ItemArchetypeTag == ItemArchetypeTag)
            {
                // Dynamic pricing elasticity formula: Price = BasePrice * (Demand / Supply)^Elasticity
                const float SafeSupply = FMath::Max(1.0f, Commodity.CurrentSupply);
                const float Ratio = FMath::Max(0.1f, Commodity.CurrentDemand / SafeSupply);
                const float ElasticMultiplier = FMath::Pow(Ratio, Commodity.PriceElasticity);
                
                return Commodity.BasePrice * ElasticMultiplier * Market->BaseInflationRate;
            }
        }
    }
    return 10.0f; // Default fallback fallback
}

void UDynamicEconomySubsystem::RegisterMarketTransaction(FGameplayTag MarketTag, FGameplayTag ItemArchetypeTag, int32 QuantitySold, int32 QuantityBought)
{
    if (FDynamicMarketRegionDef* Market = ActiveRegionalMarkets.Find(MarketTag))
    {
        for (FDynamicMarketCommodity& Commodity : Market->BaselineCommodities)
        {
            if (Commodity.ItemArchetypeTag == ItemArchetypeTag)
            {
                // Players selling increases supply; players buying increases demand
                Commodity.CurrentSupply += QuantitySold;
                Commodity.CurrentDemand += QuantityBought;
                break;
            }
        }
    }
}

void UDynamicEconomySubsystem::PostNPCWorkOrder(const FDynamicNpcWorkOrderRequest& Request)
{
    FGuid OrderGuid = FGuid::NewGuid();
    ActiveWorkOrders.Add(OrderGuid, Request);
    
    // Automatically generate a dynamic quest for players to see this work order
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (GS)
    {
        for (const auto& RulePair : GS->QuestGeneratorRules)
        {
            if (RulePair.Value.ContextProfessionOrFactionTag == Request.RequiredProfessionTag)
            {
                // If the NPC matches a guild/profession that generates quests, we can dispatch it to the town board here.
                break;
            }
        }
    }
}

bool UDynamicEconomySubsystem::FulfillWorkOrder(const FGuid& WorkOrderGuid, AActor* FulfillingPlayer)
{
    if (!FulfillingPlayer) return false;

    FDynamicNpcWorkOrderRequest Order;
    if (!ActiveWorkOrders.RemoveAndCopyValue(WorkOrderGuid, Order)) return false;

    if (UDynamicContainerComponent* Inv = FulfillingPlayer->FindComponentByClass<UDynamicContainerComponent>())
    {
        if (Inv->GetTotalCountOfArchetype(Order.DesiredItemArchetypeTag) >= Order.Quantity)
        {
            if (Inv->ServerRemoveStoredActorByTag(Order.DesiredItemArchetypeTag, Order.Quantity))
            {
                // Grant reward
                if (UDynamicSocialComponent* Social = FulfillingPlayer->FindComponentByClass<UDynamicSocialComponent>())
                {
                    Social->ServerRecordSocialInteraction(
                        Order.RequesterGuid, 
                        nullptr, 
                        ERelationshipTargetKind::Character, 
                        FGameplayTag::RequestGameplayTag(TEXT("Relationship.Type.Business"), false), 
                        TArray<FTagFloatPair>(), 
                        FGameplayTagContainer()
                    );
                }
                
                // Add gold via ability system attributes
                return true;
            }
        }
    }
    
    // Add back to pool if failed
    ActiveWorkOrders.Add(WorkOrderGuid, Order);
    return false;
}

void UDynamicEconomySubsystem::SimulateMacroEconomy(float DeltaTime)
{
    // Natural supply/demand mean-reversion and decay over time
    for (auto& MarketPair : ActiveRegionalMarkets)
    {
        FDynamicMarketRegionDef& Market = MarketPair.Value;
        for (FDynamicMarketCommodity& Commodity : Market.BaselineCommodities)
        {
            // Supplies naturally rot or are consumed over time
            Commodity.CurrentSupply = FMath::FInterpTo(Commodity.CurrentSupply, 100.0f, 1.0f, 0.05f);
            
            // Demand naturally recovers to baseline
            Commodity.CurrentDemand = FMath::FInterpTo(Commodity.CurrentDemand, 100.0f, 1.0f, 0.05f);
        }
    }
}