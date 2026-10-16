// DynamicEffectFactory.cpp
#include "DynamicEffectFactory.h"
#include "AbilitySystemComponent.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"

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

	// 2. Map Extended Target Compounding Rules (Stacking Parameters)
	switch (FoundDef->StackingPolicy)
	{
		case EGameplayEffectStackingPolicy::None:              TransientEffect->StackingType = EGameplayEffectStackingType::None; break;
		case EGameplayEffectStackingPolicy::AggregateBySource: TransientEffect->StackingType = EGameplayEffectStackingType::AggregateBySource; break;
		case EGameplayEffectStackingPolicy::AggregateByTarget: TransientEffect->StackingType = EGameplayEffectStackingType::AggregateByTarget; break;
	}
	TransientEffect->StackLimitCount = FoundDef->MaxStackCount;

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

	// 5. FIX: Instantiate using 'new' directly into the FGameplayEffectSpecHandle constructor.
	// The internal handle manages the shared pointer life cycle automatically from this raw heap address.
	FGameplayEffectContextHandle ContextHandle = SourceASC->MakeEffectContext();
	FGameplayEffectSpec* NewRawSpec = new FGameplayEffectSpec(TransientEffect, ContextHandle, Level);

	return FGameplayEffectSpecHandle(NewRawSpec);
}