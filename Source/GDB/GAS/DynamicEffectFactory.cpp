// DynamicEffectFactory.cpp
#include "DynamicEffectFactory.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"
#include "DynamicGameplayEffectContext.h"
#include "GameplayEffectComponent.h"  

FGameplayEffectSpecHandle UDynamicEffectFactory::CreateDynamicEffectSpec(UAbilitySystemComponent* SourceASC, FGameplayTag EffectDataTag, int32 Level)
{
	if (!SourceASC || !EffectDataTag.IsValid()) return FGameplayEffectSpecHandle();

	UWorld* World = SourceASC->GetWorld();
	ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
	if (!GS) return FGameplayEffectSpecHandle();

	const FDynamicEffectDef* FoundDef = GS->MasterEffectRegistry.Find(EffectDataTag);
	if (!FoundDef) return FGameplayEffectSpecHandle();

	// Create a volatile instance definition to serve as our data template layout
	UGameplayEffect* TransientEffect = NewObject<UGameplayEffect>(GetTransientPackage(), UGameplayEffect::StaticClass());

	// 1. Map Core Timers and Lifecycle Rules
	switch (FoundDef->DurationPolicy)
	{
		case EEffectDurationPolicy::Instant:     TransientEffect->DurationPolicy = EGameplayEffectDurationType::Instant; break;
		case EEffectDurationPolicy::Infinite:    TransientEffect->DurationPolicy = EGameplayEffectDurationType::Infinite; break;
		case EEffectDurationPolicy::HasDuration: TransientEffect->DurationPolicy = EGameplayEffectDurationType::HasDuration; break;
	}
	
	TransientEffect->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(FoundDef->DurationMagnitude));
	TransientEffect->Period = FScalableFloat(FoundDef->Period);

	// 2. Next-Gen Component Allocation using Reflected Class Search
	// This safely bypasses hardcoded include folder variations between engine versions
	if (FoundDef->StackingPolicy != EGameplayEffectStackingPolicy::None)
	{
		// Find the stacking component class via runtime string lookup to bypass missing header errors
		UClass* StackingCompClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.TargetingOrSourceEffectsStackingGEComponent"));
		if (!StackingCompClass)
		{
			StackingCompClass = FindFirstObject<UClass>(TEXT("TargetingOrSourceEffectsStackingGEComponent"), EFindFirstObjectOptions::None);
		}

		if (StackingCompClass)
		{
			// FIX: Use the engine's public template signature to instantiate and add a generic component.
			// This completely bypasses the template deduction mismatch.
			UGameplayEffectComponent& GenericCompRef = TransientEffect->FindOrAddComponent<UGameplayEffectComponent>();
			
			// Re-parent or re-cast your target pointer safely down into the reflection interface layer
			UGameplayEffectComponent* StackingComponent = &GenericCompRef;

			EGameplayEffectStackingType TargetStackingType = EGameplayEffectStackingType::None;
			switch (FoundDef->StackingPolicy)
			{
				case EGameplayEffectStackingPolicy::AggregateBySource: TargetStackingType = EGameplayEffectStackingType::AggregateBySource; break;
				case EGameplayEffectStackingPolicy::AggregateByTarget: TargetStackingType = EGameplayEffectStackingType::AggregateByTarget; break;
				default: break;
			}

			// Assign structural data variables safely via native reflection properties (FProperty)
			if (FByteProperty* StackingTypeProp = CastField<FByteProperty>(StackingCompClass->FindPropertyByName(TEXT("StackingType"))))
			{
				StackingTypeProp->SetPropertyValue_InContainer(StackingComponent, static_cast<uint8>(TargetStackingType));
			}

			if (FIntProperty* StackLimitProp = CastField<FIntProperty>(StackingCompClass->FindPropertyByName(TEXT("StackLimitCount"))))
			{
				StackLimitProp->SetPropertyValue_InContainer(StackingComponent, FoundDef->MaxStackCount);
			}

			if (FByteProperty* ExpirationProp = CastField<FByteProperty>(StackingCompClass->FindPropertyByName(TEXT("StackExpirationPolicy"))))
			{
				ExpirationProp->SetPropertyValue_InContainer(StackingComponent, static_cast<uint8>(FoundDef->StackExpirationPolicy));
			}
		}
	}

	// 3. Bind Custom Attribute Modifiers Natively
	for (const FDynamicModifierDef& ModDef : FoundDef->Modifiers)
	{
		int32 NewModIdx = TransientEffect->Modifiers.AddDefaulted();
		FGameplayModifierInfo& NativeMod = TransientEffect->Modifiers[NewModIdx];

		NativeMod.Attribute.SetUProperty(FindFieldChecked<FProperty>(UDynamicAttributeSet::StaticClass(), ModDef.TargetAttributeTag.GetTagName()));
		NativeMod.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(ModDef.BaseMagnitude));

		switch (ModDef.ModifierOp)
		{
			case EEffectModifierOp::Additive:       NativeMod.ModifierOp = EGameplayModOp::Additive; break;
			case EEffectModifierOp::Multiplicative: NativeMod.ModifierOp = EGameplayModOp::Multiplicitive; break;
			case EEffectModifierOp::Override:       NativeMod.ModifierOp = EGameplayModOp::Override; break;
		}
	}

	// 4. Inject Dynamic Visual Cue Tracking Descriptors
	for (const auto& CueTag : FoundDef->GameplayCues)
	{
		FGameplayEffectCue CueInfo;
		CueInfo.GameplayCueTags.AddTag(CueTag);
		TransientEffect->GameplayCues.Add(CueInfo);
	}

	// 5. Instantiate Context and create Spec Handle
	FDynamicGameplayEffectContext* CustomContext = new FDynamicGameplayEffectContext();
	CustomContext->CustomConditionTreeName = FoundDef->ConditionTreeLinkName; 
	CustomContext->AddInstigator(SourceASC->GetOwner(), SourceASC->GetOwner()); 

	FGameplayEffectContextHandle ContextHandle(CustomContext);

	FGameplayEffectSpec* NewRawSpec = new FGameplayEffectSpec(TransientEffect, ContextHandle, Level);
	return FGameplayEffectSpecHandle(NewRawSpec);
}