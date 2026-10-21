// DynamicCustomizationComponent.cpp
#include "DynamicCustomizationComponent.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
#include "DynamicAttributeSet.h"
#include "DynamicGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"
#include "Net/UnrealNetwork.h"
#include "Components/SkeletalMeshComponent.h"

UDynamicCustomizationComponent::UDynamicCustomizationComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedMorphs.OwningCustomizationComp = this;
}

void UDynamicCustomizationComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedMorphs.OwningCustomizationComp = this;
}

void UDynamicCustomizationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicCustomizationComponent, ActiveCharacterTemplateTag);
    DOREPLIFETIME(UDynamicCustomizationComponent, ActiveGender);
    DOREPLIFETIME(UDynamicCustomizationComponent, ReplicatedMorphs);
    DOREPLIFETIME(UDynamicCustomizationComponent, SkinColor);
    DOREPLIFETIME(UDynamicCustomizationComponent, HairColor);
    DOREPLIFETIME(UDynamicCustomizationComponent, ActiveHairArchetypeTag);
    DOREPLIFETIME(UDynamicCustomizationComponent, ActiveTransformations);
    DOREPLIFETIME(UDynamicCustomizationComponent, ActiveInheritedCategories);
    DOREPLIFETIME(UDynamicCustomizationComponent, bCachedInvertsHealingToDamage);
}

void FDynamicCharacterMorphItem::PreReplicatedRemove(const FFastCharacterMorphArray& InArrayOwner)
{
    if (InArrayOwner.OwningCustomizationComp)
    {
        InArrayOwner.OwningCustomizationComp->HandleSingleMorphReplicated(*this);
    }
}

void FDynamicCharacterMorphItem::PostReplicatedAdd(const FFastCharacterMorphArray& InArrayOwner)
{
    if (InArrayOwner.OwningCustomizationComp)
    {
        InArrayOwner.OwningCustomizationComp->HandleSingleMorphReplicated(*this);
    }
}

void FDynamicCharacterMorphItem::PostReplicatedChange(const FFastCharacterMorphArray& InArrayOwner)
{
    if (InArrayOwner.OwningCustomizationComp)
    {
        InArrayOwner.OwningCustomizationComp->HandleSingleMorphReplicated(*this);
    }
}

void UDynamicCustomizationComponent::HandleSingleMorphReplicated(const FDynamicCharacterMorphItem& MorphItem)
{
    LocalMorphCache.Add(MorphItem.SliderTag, MorphItem.SliderValue);
    ApplySingleSliderDeformation(MorphItem.SliderTag, MorphItem.SliderValue);
    OnMorphUpdated.Broadcast(MorphItem.SliderTag, MorphItem.SliderValue);
}

void UDynamicCustomizationComponent::OnRep_ReplicatedMorphs()
{
    // Optimize morph application: Only evaluate and push delta changes to the skeletal mesh
    // rather than resetting and rebuilding all morph targets across the character rig.
    TSet<FGameplayTag> ReplicatedTags;
    ReplicatedTags.Reserve(ReplicatedMorphs.Items.Num());

    for (const FDynamicCharacterMorphItem& Item : ReplicatedMorphs.Items)
    {
        ReplicatedTags.Add(Item.SliderTag);

        const float* CachedVal = LocalMorphCache.Find(Item.SliderTag);
        if (!CachedVal || !FMath::IsNearlyEqual(*CachedVal, Item.SliderValue, 0.001f))
        {
            LocalMorphCache.Add(Item.SliderTag, Item.SliderValue);
            ApplySingleSliderDeformation(Item.SliderTag, Item.SliderValue);
            OnMorphUpdated.Broadcast(Item.SliderTag, Item.SliderValue);
        }
    }

    // Clean up any sliders that were removed remotely
    if (LocalMorphCache.Num() > ReplicatedMorphs.Items.Num())
    {
        TArray<FGameplayTag> StaleKeys;
        for (const auto& Pair : LocalMorphCache)
        {
            if (!ReplicatedTags.Contains(Pair.Key))
            {
                StaleKeys.Add(Pair.Key);
            }
        }

        for (const FGameplayTag& DeadTag : StaleKeys)
        {
            LocalMorphCache.Remove(DeadTag);
            ApplySingleSliderDeformation(DeadTag, 0.0f);
            OnMorphUpdated.Broadcast(DeadTag, 0.0f);
        }
    }
}

void UDynamicCustomizationComponent::OnRep_CharacterIdentity()
{
    ApplyCustomizationToMesh();
}

void UDynamicCustomizationComponent::InitializeCustomization(FGameplayTag TemplateTag, ECharacterGender Gender)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;

    ActiveCharacterTemplateTag = TemplateTag;
    ActiveGender = Gender;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    FCharacterTemplateDef TemplateDef;
    if (!GS->GetCharacterTemplate(TemplateTag, TemplateDef)) return;

    ReplicatedMorphs.Items.Empty();
    LocalMorphCache.Empty();

    for (const FTagFloatPair& DefaultPair : TemplateDef.DefaultBodyslideSliders)
    {
        if (DefaultPair.KeyTag.IsValid())
        {
            const float ClampedVal = ClampSliderToLimits(DefaultPair.KeyTag, DefaultPair.Value);
            FDynamicCharacterMorphItem NewItem;
            NewItem.SliderTag = DefaultPair.KeyTag;
            NewItem.SliderValue = ClampedVal;
            FDynamicCharacterMorphItem& Added = ReplicatedMorphs.Items.Add_GetRef(NewItem);
            ReplicatedMorphs.MarkItemDirty(Added);
            LocalMorphCache.Add(DefaultPair.KeyTag, ClampedVal);
        }
    }

    ReplicatedMorphs.MarkArrayDirty();
    ApplyCustomizationToMesh();
}

float UDynamicCustomizationComponent::ClampSliderToLimits(FGameplayTag SliderTag, float RawValue) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return RawValue;

    FCharacterTemplateDef TemplateDef;
    if (!GS->GetCharacterTemplate(ActiveCharacterTemplateTag, TemplateDef)) return RawValue;

    // 1. Check gender-specific slider clamps first
    for (const FCharacterGenderProfile& Variant : TemplateDef.GenderVariants)
    {
        if (Variant.Gender == ActiveGender)
        {
            for (const FRacialSliderClampOverride& Clamp : Variant.GenderSliderClamps)
            {
                if (Clamp.SliderTag == SliderTag)
                {
                    return FMath::Clamp(RawValue, Clamp.ClampedMin, Clamp.ClampedMax);
                }
            }
        }
    }

    // 2. Fall back to racial slider clamps
    for (const FRacialSliderClampOverride& Clamp : TemplateDef.RacialSliderClamps)
    {
        if (Clamp.SliderTag == SliderTag)
        {
            return FMath::Clamp(RawValue, Clamp.ClampedMin, Clamp.ClampedMax);
        }
    }

    return FMath::Clamp(RawValue, 0.0f, 1.0f);
}

void UDynamicCustomizationComponent::ServerSetMorphSliderValue(FGameplayTag SliderTag, float Value)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !SliderTag.IsValid()) return;

    const float ClampedValue = ClampSliderToLimits(SliderTag, Value);
    LocalMorphCache.Add(SliderTag, ClampedValue);

    bool bFound = false;
    for (FDynamicCharacterMorphItem& Item : ReplicatedMorphs.Items)
    {
        if (Item.SliderTag == SliderTag)
        {
            Item.SliderValue = ClampedValue;
            ReplicatedMorphs.MarkItemDirty(Item);
            bFound = true;
            break;
        }
    }

    if (!bFound)
    {
        FDynamicCharacterMorphItem NewItem;
        NewItem.SliderTag = SliderTag;
        NewItem.SliderValue = ClampedValue;
        FDynamicCharacterMorphItem& AddedRef = ReplicatedMorphs.Items.Add_GetRef(NewItem);
        ReplicatedMorphs.MarkItemDirty(AddedRef);
    }

    ApplySingleSliderDeformation(SliderTag, ClampedValue);
    OnMorphUpdated.Broadcast(SliderTag, ClampedValue);
}

float UDynamicCustomizationComponent::GetMorphSliderValue(FGameplayTag SliderTag) const
{
    return LocalMorphCache.FindRef(SliderTag);
}

void UDynamicCustomizationComponent::ApplyCustomizationToMesh()
{
    for (const auto& Pair : LocalMorphCache)
    {
        ApplySingleSliderDeformation(Pair.Key, Pair.Value);
    }
}

void UDynamicCustomizationComponent::ApplySingleSliderDeformation(FGameplayTag SliderTag, float Value)
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (!CharOwner) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    FCharacterTemplateDef TemplateDef;
    if (!GS->GetCharacterTemplate(ActiveCharacterTemplateTag, TemplateDef)) return;

    const FDynamicSkeletonDef* SkelDef = GS->SkeletonMasterRegistry.Find(TemplateDef.SkeletonTypeTag);
    if (!SkelDef) return;

    // Collect all modular skeletal meshes attached to the character (Face, Torso, Legs, Feet)
    TArray<USkeletalMeshComponent*> ModularMeshes;
    CharOwner->GetComponents<USkeletalMeshComponent>(ModularMeshes);

    for (const FBodyslideSliderDef& SliderDef : SkelDef->BodyslideSliders)
    {
        if (SliderDef.SliderTag == SliderTag)
        {
            // 1. Morph Targets: Apply directly across all modular meshes
            for (const FName& MorphName : SliderDef.TargetMorphTargets)
            {
                if (!MorphName.IsNone())
                {
                    for (USkeletalMeshComponent* MeshComp : ModularMeshes)
                    {
                        if (MeshComp)
                        {
                            MeshComp->SetMorphTarget(MorphName, Value);
                        }
                    }
                }
            }

            // 2. Bone Scales: Propagate bone scale and length adjustments via curves to the AnimBP/ControlRig
            for (const FBoneScaleConstraint& BoneConstraint : SliderDef.TargetBones)
            {
                if (!BoneConstraint.BoneName.IsNone())
                {
                    const float TargetScale = FMath::Lerp(BoneConstraint.MinScaleMultiplier, BoneConstraint.MaxScaleMultiplier, Value);

                    for (USkeletalMeshComponent* MeshComp : ModularMeshes)
                    {
                        if (MeshComp && MeshComp->DoesSocketExist(BoneConstraint.BoneName))
                        {
                            const FName DirectBoneCurve = BoneConstraint.BoneName;
                            const FName ScaleCurveName = FName(*FString::Printf(TEXT("scale_%s"), *BoneConstraint.BoneName.ToString()));

                            MeshComp->SetMorphTarget(ScaleCurveName, TargetScale);
                            MeshComp->SetMorphTarget(DirectBoneCurve, TargetScale);

                            if (BoneConstraint.bAllowLengthAdjustment)
                            {
                                const float TargetLength = FMath::Lerp(BoneConstraint.MinLengthOffset, BoneConstraint.MaxLengthOffset, Value);
                                const FName LengthCurveName = FName(*FString::Printf(TEXT("length_%s"), *BoneConstraint.BoneName.ToString()));
                                MeshComp->SetMorphTarget(LengthCurveName, TargetLength);
                            }
                        }
                    }
                }
            }
            break;
        }
    }
}

void UDynamicCustomizationComponent::OnRep_AppearanceColors()
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (CharOwner && CharOwner->GetMeshComponent())
    {
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("SkinColorPrimary"), FVector(SkinColor.PrimaryColor));
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("SkinColorSecondary"), FVector(SkinColor.SecondaryColor));
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("HairColorPrimary"), FVector(HairColor.PrimaryColor));
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("HairColorSecondary"), FVector(HairColor.SecondaryColor));
    }
}

void UDynamicCustomizationComponent::OnRep_HairStyle()
{
    // Client-side hair mesh synchronization hook
}

void UDynamicCustomizationComponent::ServerApplyTransformation(FGameplayTag TransformationTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TransformationTag.IsValid()) return;
    if (ActiveTransformations.HasTagExact(TransformationTag)) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicTransformationRule* Rule = GS->ConversionRegistry.Find(TransformationTag);
    if (!Rule) return;

    // Note: Uses hierarchical MatchesTag so Character.Humanoid.Elf matches AllowedBaseTemplateTags = [Character.Humanoid]
    if (Rule->AllowedBaseTemplateTags.Num() > 0 && !ActiveCharacterTemplateTag.MatchesAny(Rule->AllowedBaseTemplateTags))
    {
        return;
    }

    ActiveTransformations.AddTag(TransformationTag);
    ActiveInheritedCategories.AppendTags(Rule->InheritedCategoryTags);
    if (Rule->bInvertsHealingToDamage)
    {
        bCachedInvertsHealingToDamage = true;
    }

    if (Rule->bApplyColorTint)
    {
        SkinColor = Rule->TransformationColorTint;
        OnRep_AppearanceColors();
    }

    if (ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner()))
    {
        if (UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet()))
        {
            for (const FGenderAttributeModifier& Mod : Rule->TransformationStatModifiers)
            {
                if (Mod.TargetAttributeTag.IsValid())
                {
                    const float CurVal = AttrSet->GetDynamicAttributeValue(Mod.TargetAttributeTag, 0);
                    float NewVal = CurVal;
                    if (Mod.ModifierOp == EEffectModifierOp::Additive)
                    {
                        NewVal = CurVal + Mod.Magnitude;
                    }
                    else if (Mod.ModifierOp == EEffectModifierOp::Multiplicative)
                    {
                        NewVal = CurVal * (1.0f + Mod.Magnitude);
                    }
                    else if (Mod.ModifierOp == EEffectModifierOp::Override)
                    {
                        NewVal = Mod.Magnitude;
                    }
                    AttrSet->SetDynamicAttributeValue(Mod.TargetAttributeTag, NewVal, 0);
                }
            }
        }

        if (UAbilitySystemComponent* ASC = CharOwner->GetAbilitySystemComponent())
        {
            // Also grant InheritedCategoryTags as loose gameplay tags so ConditionTrees can query SuperCategory.Undead
            ASC->AddLooseGameplayTags(Rule->InheritedCategoryTags);

            for (const FGameplayTag& AbilityTag : Rule->GrantedAbilities)
            {
                if (const FDynamicAbilityDef* AbilityDef = GS->AbilityMasterRegistry.Find(AbilityTag))
                {
                    FGameplayAbilitySpec Spec(UDynamicGameplayAbility::StaticClass(), 1, INDEX_NONE, CharOwner);
                    Spec.GetDynamicSpecSourceTags().AddTag(AbilityTag);
                    FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
                    if (FGameplayAbilitySpec* GrantedSpec = ASC->FindAbilitySpecFromHandle(Handle))
                    {
                        if (UDynamicGameplayAbility* Instanced = Cast<UDynamicGameplayAbility>(GrantedSpec->GetPrimaryInstance()))
                        {
                            Instanced->ActiveAbilityRegistryTag = AbilityTag;
                            Instanced->AbilityConfig = *AbilityDef;
                        }
                    }
                }
            }
        }
    }
}

void UDynamicCustomizationComponent::ServerSireConversion(UDynamicCustomizationComponent* TargetCustomization, FGameplayTag TransformationTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !TargetCustomization) return;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    const FDynamicTransformationRule* Rule = GS->ConversionRegistry.Find(TransformationTag);
    if (!Rule) return;

    TargetCustomization->ServerApplyTransformation(TransformationTag);

    if (Rule->bSiringDrainsMaxHP)
    {
        if (ADynamicCharacter* SireChar = Cast<ADynamicCharacter>(GetOwner()))
        {
            if (UDynamicAttributeSet* SireAttr = const_cast<UDynamicAttributeSet*>(SireChar->GetDynamicAttributeSet()))
            {
                static const FGameplayTag MaxHpTag = FGameplayTag::RequestGameplayTag(FName("Attribute.Resource.Health.Max"), false);
                if (MaxHpTag.IsValid())
                {
                    const float CurMaxHP = SireAttr->GetDynamicAttributeValue(MaxHpTag, 0);
                    const float PenalizedHP = FMath::Max(10.0f, CurMaxHP - Rule->SiringMaxHPPenaltyAmount);
                    SireAttr->SetDynamicAttributeValue(MaxHpTag, PenalizedHP, 0);
                }
            }
        }
    }
}

bool UDynamicCustomizationComponent::ServerCheckAndTriggerEvolution(FGameplayTag BaseTemplateTag)
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return false;

    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    // Look up available evolutionary branches for this base template tag
    const FDynamicEvolutionContainer* EvoContainer = GS->EvolutionMasterRegistry.Find(BaseTemplateTag);
    if (!EvoContainer) return false;

    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (!CharOwner) return false;

    UAbilitySystemComponent* ASC = CharOwner->GetAbilitySystemComponent();
    UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet());
    if (!ASC || !AttrSet) return false;

    for (const FDynamicEvolutionBranch& Branch : EvoContainer->EvolutionBranches)
    {
        bool bPassesCheck = true;

        // 1. Evaluate Attribute Threshold (e.g. Level >= 5)
        if (Branch.RequiredAttributeTag.IsValid())
        {
            float CurVal = AttrSet->GetDynamicAttributeValue(Branch.RequiredAttributeTag, 0);
            if (CurVal < Branch.RequiredAttributeMinimumValue)
            {
                bPassesCheck = false;
            }
        }

        // 2. Evaluate Logic Condition Tree (e.g. Starvation or Experience checks)
        if (bPassesCheck && !Branch.PrerequisiteConditionTreeName.IsNone())
        {
            const FDynamicConditionTreeContainer* Tree = UDynamicGASManagerComponent::GetConditionTreeByName(CharOwner, Branch.PrerequisiteConditionTreeName);
            if (Tree)
            {
                FGameplayTagContainer SourceTags, TargetTags;
                ASC->GetOwnedGameplayTags(TargetTags);
                if (!UDynamicLogicEvaluator::EvaluateConditionTree(*Tree, ASC, ASC, SourceTags, TargetTags))
                {
                    bPassesCheck = false;
                }
            }
        }

        // 3. Trigger Evolution if all conditions are met
        if (bPassesCheck)
        {
            // Mutate template tag
            ActiveCharacterTemplateTag = Branch.TargetCharacterTemplateTag;

            // Apply evolution stat modifications
            for (const FDynamicModifierDef& Mod : Branch.EvolutionStatModifiers)
            {
                if (Mod.TargetAttributeTag.IsValid())
                {
                    float Cur = AttrSet->GetDynamicAttributeValue(Mod.TargetAttributeTag, 0);
                    AttrSet->SetDynamicAttributeValue(Mod.TargetAttributeTag, Cur + Mod.BaseMagnitude, 0);
                }
            }

            // Reinitialize mesh and appearance for the new template
            FCharacterTemplateDef NewTemplate;
            if (GS->GetCharacterTemplate(Branch.TargetCharacterTemplateTag, NewTemplate))
            {
                if (USkeletalMeshComponent* MeshComp = CharOwner->GetMeshComponent())
                {
                    if (!NewTemplate.SkeletalMeshAsset.IsNull())
                    {
                        if (USkeletalMesh* NewMesh = NewTemplate.SkeletalMeshAsset.LoadSynchronous())
                        {
                            MeshComp->SetSkeletalMeshAsset(NewMesh);
                        }
                    }
                }
            }
            return true;
        }
    }
    return false;
}