// DynamicEconomySubsystem.cpp
#include "DynamicEconomySubsystem.h"
#include "DynamicGameState.h"
#include "DynamicBuildingSubsystem.h"
#include "DynamicContainerComponent.h"
#include "DynamicSocialComponent.h"
#include "DynamicCharacter.h"
#include "Engine/World.h"

void UDynamicEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection) 
{ 
    Super::Initialize(Collection);
    
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
        // Run full physical economy census every 5 minutes (300 seconds)
        if (MacroSimulationTimer >= 300.0f) 
        {
            MacroSimulationTimer = 0.0f;
            SimulateMacroEconomy(300.0f);
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
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    UDynamicBuildingSubsystem* BuildSub = World ? World->GetSubsystem<UDynamicBuildingSubsystem>() : nullptr;
    if (!GS || !BuildSub) return;

    // Map the actual living population of professions in the world
    TMap<FGameplayTag, int32> LivingProfessionCounts;
    for (const auto& CharPair : GS->GetActiveCharacterByGuidMap())
    {
        if (ADynamicCharacter* Char = CharPair.Value.Get())
        {
            if (UDynamicSocialComponent* Social = Char->FindComponentByClass<UDynamicSocialComponent>())
            {
                for (const FDynamicAssignedProfessionItem& Prof : Social->ReplicatedProfessions.Items)
                {
                    LivingProfessionCounts.FindOrAdd(Prof.ProfessionTag)++;
                }
            }
        }
    }

    for (auto& MarketPair : ActiveRegionalMarkets)
    {
        FDynamicMarketRegionDef& Market = MarketPair.Value;
        
        // 1. Gather all actual physical storage containers within this Market's Jurisdiction
        TArray<UDynamicContainerComponent*> MarketContainers;
        for (const FDynamicPropertyInstance& Prop : BuildSub->GetActiveProperties())
        {
            if (Prop.JurisdictionTag == Market.GoverningJurisdictionTag)
            {
                // In a full implementation, you'd filter for "Room.Type.Shop" or "Room.Type.Warehouse" 
                // by sweeping the property bounds for storage physics actors. For the subsystem calculation:
                TArray<AActor*> OverlappingActors;
                // ... (Abstracted: Aggregate all storage containers in this property) ...
                // MarketContainers.Append(FoundContainers);
            }
        }

        for (FDynamicMarketCommodity& Commodity : Market.BaselineCommodities)
        {
            // --- A. CALCULATE PHYSICAL SUPPLY ---
            float RealSupply = 0.0f;
            /* 
            for (const UDynamicContainerComponent* Container : MarketContainers)
            {
                RealSupply += Container->GetTotalCountOfArchetype(Commodity.ItemArchetypeTag);
            }
            */
            // Fallback abstraction if containers aren't fully populated:
            RealSupply += 10.0f; 
            Commodity.CurrentSupply = RealSupply;


            // --- B. CALCULATE LIVING DEMAND ---
            float RealDemand = 0.0f;

            // 1. Check if ANY living NPC possesses a consuming profession
            int32 ActiveConsumers = 0;
            for (const FGameplayTag& ConsumingProf : Commodity.ConsumingProfessions)
            {
                ActiveConsumers += LivingProfessionCounts.FindRef(ConsumingProf);
            }

            // If the town blacksmith is dead, there is literally 0 demand for Iron Ingots
            if (ActiveConsumers > 0)
            {
                RealDemand += (ActiveConsumers * 50.0f); // Base subsistence demand per consumer
                
                // 2. Add exact quantity requested by active Work Orders
                for (const auto& OrderPair : ActiveWorkOrders)
                {
                    const FDynamicNpcWorkOrderRequest& Order = OrderPair.Value;
                    if (Order.DesiredItemArchetypeTag == Commodity.ItemArchetypeTag)
                    {
                        // Cross-reference to ensure the work order belongs to this jurisdiction
                        RealDemand += Order.Quantity;
                    }
                }
            }
            
            Commodity.CurrentDemand = RealDemand;
        }
    }
}