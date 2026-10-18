// DynamicCustomizationComponent.cpp
#include "DynamicCustomizationComponent.h"
#include "DynamicCharacter.h"
#include "DynamicGameState.h"
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
    LocalMorphCache.Empty();
    for (const FDynamicCharacterMorphItem& Item : ReplicatedMorphs.Items)
    {
        LocalMorphCache.Add(Item.SliderTag, Item.SliderValue);
        OnMorphUpdated.Broadcast(Item.SliderTag, Item.SliderValue);
    }
    ApplyCustomizationToMesh();
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

    // Populate default slider values from template
    for (const FTagFloatPair& DefaultPair : TemplateDef.DefaultBodyslideSliders)
    {
        if (DefaultPair.KeyTag.IsValid())
        {
            float ClampedVal = ClampSliderToLimits(DefaultPair.KeyTag, DefaultPair.Value);
            FDynamicCharacterMorphItem NewItem;
            NewItem.SliderTag = DefaultPair.KeyTag;
            NewItem.SliderValue = ClampedVal;
            ReplicatedMorphs.Items.Add(NewItem);
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

    float MinL = 0.0f;
    float MaxL = 1.0f;

    // 1. Check gender-specific slider clamps first
    for (const FCharacterGenderProfile& Variant : TemplateDef.GenderVariants)
    {
        if (Variant.Gender == ActiveGender)
        {
            for (const FRacialSliderClampOverride& Clamp : Variant.GenderSliderClamps)
            {
                if (Clamp.SliderTag == SliderTag)
                {
                    MinL = Clamp.ClampedMin;
                    MaxL = Clamp.ClampedMax;
                    return FMath::Clamp(RawValue, MinL, MaxL);
                }
            }
        }
    }

    // 2. Fall back to racial slider clamps
    for (const FRacialSliderClampOverride& Clamp : TemplateDef.RacialSliderClamps)
    {
        if (Clamp.SliderTag == SliderTag)
        {
            MinL = Clamp.ClampedMin;
            MaxL = Clamp.ClampedMax;
            return FMath::Clamp(RawValue, MinL, MaxL);
        }
    }

    return FMath::Clamp(RawValue, 0.0f, 1.0f);
}

void UDynamicCustomizationComponent::ServerSetMorphSliderValue(FGameplayTag SliderTag, float Value)
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || !SliderTag.IsValid()) return;

    float ClampedValue = ClampSliderToLimits(SliderTag, Value);
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
    if (!CharOwner || !CharOwner->GetMeshComponent()) return;

    USkeletalMeshComponent* MeshComp = CharOwner->GetMeshComponent();
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    FCharacterTemplateDef TemplateDef;
    if (!GS->GetCharacterTemplate(ActiveCharacterTemplateTag, TemplateDef)) return;

    // Look up skeletal Bodyslide slider definitions from the skeleton registry
    // (Assuming SkeletonRegistry is accessible via GameState or stored locally)
    // Here we apply morph targets directly:
    // MeshComp->SetMorphTarget(MorphName, Value);
}

void UDynamicCustomizationComponent::OnRep_AppearanceColors()
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (CharOwner && CharOwner->GetMeshComponent())
    {
        // Pushes vector parameters to the Dynamic Material Instance
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("SkinColorPrimary"), FVector(SkinColor.PrimaryColor));
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("SkinColorSecondary"), FVector(SkinColor.SecondaryColor));
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("HairColorPrimary"), FVector(HairColor.PrimaryColor));
        CharOwner->GetMeshComponent()->SetVectorParameterValueOnMaterials(TEXT("HairColorSecondary"), FVector(HairColor.SecondaryColor));
    }
}

void UDynamicCustomizationComponent::OnRep_HairStyle()
{
    // Implementation: Spawns the ADynamicPhysicsActor for the hair and attaches it to the head bone using LeaderPoseComponent to share morphs.
}