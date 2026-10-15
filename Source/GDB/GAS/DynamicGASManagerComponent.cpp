// DynamicGASManagerComponent.cpp
#include "DynamicGASManagerComponent.h"
#include "DynamicGameState.h"

UDynamicGASManagerComponent::UDynamicGASManagerComponent() {}

float UDynamicGASManagerComponent::ResolveMetric(FGameplayTag ConfigProfileTag, FGameplayTag MetricTag, const FDynamicTagFloatMapWrapper& Modifiers) const
{
	float CombinedValue = 0.0f;
	if (UWorld* World = GetWorld())
	{
		if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
		{
			if (const FDynamicProjectileArchetype* ProjBase = GS->ProjectileArchetypes.Find(ConfigProfileTag))
			{
				CombinedValue = ProjBase->Metrics.LocalLookupMap.FindRef(MetricTag);
			}
			else if (const FDynamicAoeArchetype* AoeBase = GS->AoeArchetypes.Find(ConfigProfileTag))
			{
				CombinedValue = AoeBase->Metrics.LocalLookupMap.FindRef(MetricTag);
			}
		}
	}
	if (const float* ModifierVal = Modifiers.LocalLookupMap.Find(MetricTag))
	{
		CombinedValue += *ModifierVal;
	}
	return CombinedValue;
}

const FDynamicConditionTreeContainer* UDynamicGASManagerComponent::GetConditionTreeByName(const UObject* WorldContextObject, FName ConditionName)
{
	if (!WorldContextObject || ConditionName.IsNone()) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
		{
			return GS->SharedConditionTrees.Find(ConditionName);
		}
	}
	return nullptr;
}