// DynamicCustomizationComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicCustomizationComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCharacterMorphUpdatedSignature, FGameplayTag, SliderTag, float, NewValue);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GDB_API UDynamicCustomizationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDynamicCustomizationComponent();

    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Customization")
    FOnCharacterMorphUpdatedSignature OnMorphUpdated;

    // Active Template and Gender governing slider clamps and base configuration
    UPROPERTY(ReplicatedUsing = OnRep_CharacterIdentity, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FGameplayTag ActiveCharacterTemplateTag;

    UPROPERTY(ReplicatedUsing = OnRep_CharacterIdentity, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    ECharacterGender ActiveGender = ECharacterGender::Neutral;

    // Replicated fast array containing all unique custom slider values for this character instance
    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedMorphs)
    FFastCharacterMorphArray ReplicatedMorphs;

    UFUNCTION()
    void OnRep_CharacterIdentity();

    UFUNCTION()
    void OnRep_ReplicatedMorphs();

    // Fast array item callback handler
    void HandleSingleMorphReplicated(const FDynamicCharacterMorphItem& MorphItem);

    // Initializes character identity and applies default template/gender sliders
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Customization")
    void InitializeCustomization(FGameplayTag TemplateTag, ECharacterGender Gender);

    // Sets a specific Bodyslide slider value (clamps to racial/gender limits) and marks dirty for replication
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Customization")
    void ServerSetMorphSliderValue(FGameplayTag SliderTag, float Value);

    // Applies all active morphs and bone scaling/length offsets to the character's SkeletalMeshComponent
    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Customization")
    void ApplyCustomizationToMesh();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Customization")
    float GetMorphSliderValue(FGameplayTag SliderTag) const;
    
    UPROPERTY(ReplicatedUsing = OnRep_AppearanceColors, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FDynamicColorGradient SkinColor;

    UPROPERTY(ReplicatedUsing = OnRep_AppearanceColors, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FDynamicColorGradient HairColor;

    UPROPERTY(ReplicatedUsing = OnRep_HairStyle, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FGameplayTag ActiveHairArchetypeTag;

    UFUNCTION()
    void OnRep_AppearanceColors();

    UFUNCTION()
    void OnRep_HairStyle();

private:
    float ClampSliderToLimits(FGameplayTag SliderTag, float RawValue) const;
    void ApplySingleSliderDeformation(FGameplayTag SliderTag, float Value);

    TMap<FGameplayTag, float> LocalMorphCache;
};