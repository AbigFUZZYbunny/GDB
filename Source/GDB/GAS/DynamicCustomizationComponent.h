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
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(BlueprintAssignable, Category = "Dynamic GAS|Customization")
    FOnCharacterMorphUpdatedSignature OnMorphUpdated;

    UPROPERTY(ReplicatedUsing = OnRep_CharacterIdentity, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FGameplayTag ActiveCharacterTemplateTag;

    UPROPERTY(ReplicatedUsing = OnRep_CharacterIdentity, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    ECharacterGender ActiveGender = ECharacterGender::Neutral;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedMorphs)
    FFastCharacterMorphArray ReplicatedMorphs;

    UPROPERTY(ReplicatedUsing = OnRep_AppearanceColors, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FDynamicColorGradient SkinColor;

    UPROPERTY(ReplicatedUsing = OnRep_AppearanceColors, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FDynamicColorGradient HairColor;

    UPROPERTY(ReplicatedUsing = OnRep_HairStyle, BlueprintReadOnly, Category = "Dynamic GAS|Customization")
    FGameplayTag ActiveHairArchetypeTag;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Transformation")
    FGameplayTagContainer ActiveTransformations;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic GAS|Transformation")
    FGameplayTagContainer ActiveInheritedCategories;

    UFUNCTION() void OnRep_CharacterIdentity();
    UFUNCTION() void OnRep_ReplicatedMorphs();
    UFUNCTION() void OnRep_AppearanceColors();
    UFUNCTION() void OnRep_HairStyle();

    void HandleSingleMorphReplicated(const FDynamicCharacterMorphItem& MorphItem);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Customization")
    void InitializeCustomization(FGameplayTag TemplateTag, ECharacterGender Gender);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Customization")
    void ServerSetMorphSliderValue(FGameplayTag SliderTag, float Value);

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Customization")
    void ApplyCustomizationToMesh();

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Customization")
    float GetMorphSliderValue(FGameplayTag SliderTag) const;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Transformation")
    void ServerApplyTransformation(FGameplayTag TransformationTag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Transformation")
    void ServerSireConversion(UDynamicCustomizationComponent* TargetCustomization, FGameplayTag TransformationTag);

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Transformation")
    bool DoesCharacterInvertHealingToDamage() const { return bCachedInvertsHealingToDamage; }

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic GAS|Evolution")
    bool ServerCheckAndTriggerEvolution(FGameplayTag BaseTemplateTag);

    // --- Significance & Crowd Hub Optimization ---
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dynamic GAS|Customization|Significance")
    float MorphSignificanceRadius = 2000.0f;

    UFUNCTION(BlueprintPure, Category = "Dynamic GAS|Customization|Significance")
    bool CheckCurrentSignificance() const;

    UFUNCTION(BlueprintCallable, Category = "Dynamic GAS|Customization|Significance")
    void SetSignificanceState(bool bNewSignificant);

    void FlushDeferredMorphs();

private:
    float ClampSliderToLimits(FGameplayTag SliderTag, float RawValue) const;
    void ApplySingleSliderDeformation(FGameplayTag SliderTag, float Value);
    void UpdateSignificanceEvaluation();

    TMap<FGameplayTag, float> LocalMorphCache;
    TArray<FGameplayTag> DeferredMorphSliders; // Replaced TSet with TArray for iterative slicing
    bool bIsSignificantlyVisible = true;
    
    FTimerHandle TimerHandle_SignificancePoll;
    FTSTicker::FDelegateHandle FlushTickerHandle;

    UPROPERTY(Replicated)
    bool bCachedInvertsHealingToDamage = false;
};