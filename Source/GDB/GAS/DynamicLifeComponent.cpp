// DynamicLifeComponent.cpp
#include "DynamicLifeComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicGASManagerComponent.h"
#include "Net/UnrealNetwork.h"

UDynamicLifeComponent::UDynamicLifeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedGenome.OwningLifeComp = this;
}

void UDynamicLifeComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedGenome.OwningLifeComp = this;

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        if (BirthWorldTime <= 0.0)
        {
            BirthWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        }

        GetWorld()->GetTimerManager().SetTimer(TimerHandle_GrowthTick, this, &UDynamicLifeComponent::ServerEvaluateContinuousGrowth, 60.0f, true);
        ServerEvaluateContinuousGrowth();
    }
}

void UDynamicLifeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicLifeComponent, BirthWorldTime);
    DOREPLIFETIME(UDynamicLifeComponent, CurrentLocomotionAnimTag);
    DOREPLIFETIME(UDynamicLifeComponent, ReplicatedGenome);
}

void FDynamicGenePairItem::PreReplicatedRemove(const FFastGenomeArray& InArrayOwner) {}
void FDynamicGenePairItem::PostReplicatedAdd(const FFastGenomeArray& InArrayOwner) { if (InArrayOwner.OwningLifeComp) InArrayOwner.OwningLifeComp->HandleGeneReplicated(*this); }
void FDynamicGenePairItem::PostReplicatedChange(const FFastGenomeArray& InArrayOwner) { if (InArrayOwner.OwningLifeComp) InArrayOwner.OwningLifeComp->HandleGeneReplicated(*this); }

void UDynamicLifeComponent::HandleGeneReplicated(const FDynamicGenePairItem& Item)
{
    OnGeneExpressed.Broadcast(Item.LocusTag, Item.ExpressedAllele);
}

void UDynamicLifeComponent::OnRep_AgeData()
{
    ApplyLerpedGrowthState(GetNormalizedAge());
}

void UDynamicLifeComponent::OnRep_Locomotion()
{
    OnContinuousGrowthUpdated.Broadcast(1.0f, 1.0f, CurrentLocomotionAnimTag);
}

void UDynamicLifeComponent::OnRep_Genome()
{
    ApplyGenePhenotypes();
}

float UDynamicLifeComponent::GetCurrentAgeYears() const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return 0.0f;

    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const double ElapsedRealSecs = FMath::Max(0.0, Now - BirthWorldTime);

    const float GameSecs = ElapsedRealSecs * GS->CalendarConfig.TimeScalar;
    const float SecsPerDay = GS->CalendarConfig.HoursPerDay * 3600.0f;
    const float DaysPerYear = GS->CalendarConfig.DaysPerSeason * GS->CalendarConfig.SeasonsPerYear;

    return (SecsPerDay > 0.0f && DaysPerYear > 0.0f) ? (GameSecs / (SecsPerDay * DaysPerYear)) : 0.0f;
}

float UDynamicLifeComponent::GetNormalizedAge() const
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!CharOwner || !GS) return 0.0f;

    UDynamicCustomizationComponent* CustComp = CharOwner->GetCustomizationComponent();
    if (!CustComp) return 0.0f;

    FCharacterTemplateDef Def;
    if (GS->GetCharacterTemplate(CustComp->ActiveCharacterTemplateTag, Def))
    {
        return FMath::Clamp(GetCurrentAgeYears() / FMath::Max(0.01f, Def.AgingProfile.ExpectedLifespanYears), 0.0f, 1.0f);
    }
    return 0.0f;
}

void UDynamicLifeComponent::ServerEvaluateContinuousGrowth()
{
    if (!GetOwner()->HasAuthority()) return;
    ApplyLerpedGrowthState(GetNormalizedAge());
}

void UDynamicLifeComponent::ApplyLerpedGrowthState(float NormalizedAge)
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!CharOwner || !GS) return;

    UDynamicCustomizationComponent* CustComp = CharOwner->GetCustomizationComponent();
    UDynamicAttributeSet* AttrSet = const_cast<UDynamicAttributeSet*>(CharOwner->GetDynamicAttributeSet());
    if (!CustComp || !AttrSet) return;

    FCharacterTemplateDef Def;
    if (!GS->GetCharacterTemplate(CustComp->ActiveCharacterTemplateTag, Def)) return;
    if (Def.AgingProfile.GrowthPhases.Num() == 0) return;

    // Cache un-aged baseline slider values to stop drift
    if (!bBaseMorphsCached)
    {
        for (const FTagFloatPair& SliderPair : Def.DefaultBodyslideSliders)
        {
            if (SliderPair.KeyTag.IsValid())
            {
                CachedBaseMorphSliders.Add(SliderPair.KeyTag, CustComp->GetMorphSliderValue(SliderPair.KeyTag));
            }
        }
        bBaseMorphsCached = true;
    }

    FContinuousGrowthKeyframe FrameA = Def.AgingProfile.GrowthPhases[0];
    FContinuousGrowthKeyframe FrameB = Def.AgingProfile.GrowthPhases.Last();
    float LerpAlpha = 0.0f;

    for (int32 i = 0; i < Def.AgingProfile.GrowthPhases.Num() - 1; ++i)
    {
        if (NormalizedAge >= Def.AgingProfile.GrowthPhases[i].NormalizedAge && NormalizedAge <= Def.AgingProfile.GrowthPhases[i+1].NormalizedAge)
        {
            FrameA = Def.AgingProfile.GrowthPhases[i];
            FrameB = Def.AgingProfile.GrowthPhases[i+1];
            const float Range = FrameB.NormalizedAge - FrameA.NormalizedAge;
            LerpAlpha = (Range > 0.0f) ? (NormalizedAge - FrameA.NormalizedAge) / Range : 0.0f;
            break;
        }
    }

    // 1. Lerp Physical Mesh Scale
    const float TargetScale = FMath::Lerp(FrameA.ScaleMultiplier, FrameB.ScaleMultiplier, LerpAlpha);
    if (CharOwner->GetMeshComponent())
    {
        CharOwner->GetMeshComponent()->SetRelativeScale3D(FVector(TargetScale));
    }

    // 2. Authoritative Locomotion Tag Synchronization
    if (CharOwner->HasAuthority() && CurrentLocomotionAnimTag != FrameA.LocomotionAnimTag)
    {
        CurrentLocomotionAnimTag = FrameA.LocomotionAnimTag;
    }

    // 3. Morph Slider Evaluation without Drift Accumulation
    for (const FTagFloatPair& MorphA : FrameA.MorphTargetOffsets)
    {
        float ValB = MorphA.Value;
        for (const FTagFloatPair& Mb : FrameB.MorphTargetOffsets)
        {
            if (Mb.KeyTag == MorphA.KeyTag)
            {
                ValB = Mb.Value;
                break;
            }
        }
        const float BlendedMorphOffset = FMath::Lerp(MorphA.Value, ValB, LerpAlpha);

        // Add offset strictly to pristine BaseSlide, avoiding compound accretion
        const float BaseSlide = CachedBaseMorphSliders.FindRef(MorphA.KeyTag);
        CustComp->ServerSetMorphSliderValue(MorphA.KeyTag, BaseSlide + BlendedMorphOffset);
    }

    // 4. Update Physics Motors & Attributes
    const float TargetMotorMulti = FMath::Lerp(FrameA.PhysicsMotorMultiplier, FrameB.PhysicsMotorMultiplier, LerpAlpha);

    for (const FTagFloatPair& StatA : FrameA.StatModifiers)
    {
        float ValB = StatA.Value;
        for (const FTagFloatPair& Sb : FrameB.StatModifiers)
        {
            if (Sb.KeyTag == StatA.KeyTag)
            {
                ValB = Sb.Value;
                break;
            }
        }
        const float BlendedStat = FMath::Lerp(StatA.Value, ValB, LerpAlpha);
        AttrSet->SetDynamicAttributeValue(StatA.KeyTag, BlendedStat, 0);
    }

    OnContinuousGrowthUpdated.Broadcast(TargetScale, TargetMotorMulti, CurrentLocomotionAnimTag);
}

FGameplayTag UDynamicLifeComponent::ResolveDominance(FGameplayTag Locus, FGameplayTag AlleleA, FGameplayTag AlleleB) const
{
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    if (!GS || !CharOwner) return AlleleA;

    const FDynamicGeneLocusDef* LocusDef = GS->GeneticsMasterRegistry.Find(Locus);
    if (!LocusDef) return AlleleA;

    float WeightA = 0.0f, WeightB = 0.0f;

    if (UDynamicCustomizationComponent* CustComp = CharOwner->GetCustomizationComponent())
    {
        FCharacterTemplateDef Def;
        if (GS->GetCharacterTemplate(CustComp->ActiveCharacterTemplateTag, Def))
        {
            Def.AlleleDominanceOverrides.SyncMapFromChangesArray();
            if (const float* OverrideA = Def.AlleleDominanceOverrides.LocalLookupMap.Find(AlleleA)) WeightA = *OverrideA;
            if (const float* OverrideB = Def.AlleleDominanceOverrides.LocalLookupMap.Find(AlleleB)) WeightB = *OverrideB;
        }
    }

    for (const FDynamicAlleleDef& Allele : LocusDef->PossibleAlleles)
    {
        if (WeightA == 0.0f && Allele.AlleleTag == AlleleA) WeightA = Allele.DominanceWeight;
        if (WeightB == 0.0f && Allele.AlleleTag == AlleleB) WeightB = Allele.DominanceWeight;
    }

    if (FMath::IsNearlyEqual(WeightA, WeightB)) return FMath::RandBool() ? AlleleA : AlleleB;
    return (WeightA > WeightB) ? AlleleA : AlleleB;
}

void UDynamicLifeComponent::ServerInheritGenetics(const UDynamicLifeComponent* Mother, const UDynamicLifeComponent* Father)
{
    if (!GetOwner()->HasAuthority() || !Mother || !Father) return;
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return;

    ReplicatedGenome.Items.Empty();

    for (const auto& LocusPair : GS->GeneticsMasterRegistry)
    {
        const FGameplayTag TargetLocus = LocusPair.Key;
        const FDynamicGeneLocusDef& LocusDef = LocusPair.Value;
        FGameplayTag MomContributed, DadContributed;

        for (const FDynamicGenePairItem& MomGene : Mother->ReplicatedGenome.Items)
        {
            if (MomGene.LocusTag == TargetLocus)
            {
                MomContributed = FMath::RandBool() ? MomGene.MaternalAllele : MomGene.PaternalAllele;
                break;
            }
        }

        for (const FDynamicGenePairItem& DadGene : Father->ReplicatedGenome.Items)
        {
            if (DadGene.LocusTag == TargetLocus)
            {
                DadContributed = FMath::RandBool() ? DadGene.MaternalAllele : DadGene.PaternalAllele;
                break;
            }
        }

        if (FMath::FRand() <= LocusDef.MutationChance && LocusDef.PossibleAlleles.Num() > 0)
        {
            const int32 MutIdx = FMath::RandRange(0, LocusDef.PossibleAlleles.Num() - 1);
            MomContributed = LocusDef.PossibleAlleles[MutIdx].AlleleTag;
        }

        FDynamicGenePairItem ChildGene;
        ChildGene.LocusTag = TargetLocus;
        ChildGene.MaternalAllele = MomContributed;
        ChildGene.PaternalAllele = DadContributed;
        ChildGene.ExpressedAllele = ResolveDominance(TargetLocus, MomContributed, DadContributed);

        FDynamicGenePairItem& Added = ReplicatedGenome.Items.Add_GetRef(ChildGene);
        ReplicatedGenome.MarkItemDirty(Added);
    }

    ApplyGenePhenotypes();
}

void UDynamicLifeComponent::ApplyGenePhenotypes()
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!CharOwner || !GS) return;

    UDynamicCustomizationComponent* CustComp = CharOwner->GetCustomizationComponent();
    UAbilitySystemComponent* ASC = CharOwner->GetAbilitySystemComponent();

    for (const FDynamicGenePairItem& Gene : ReplicatedGenome.Items)
    {
        if (const FDynamicGeneLocusDef* Locus = GS->GeneticsMasterRegistry.Find(Gene.LocusTag))
        {
            for (const FDynamicAlleleDef& Allele : Locus->PossibleAlleles)
            {
                if (Allele.AlleleTag == Gene.ExpressedAllele)
                {
                    if (CustComp && (Allele.PhenotypeColor.PrimaryColor != FLinearColor::White || Allele.PhenotypeColor.SecondaryColor != FLinearColor::White))
                    {
                        CustComp->SkinColor = Allele.PhenotypeColor;
                        CustComp->OnRep_AppearanceColors();
                    }
                    if (ASC && CharOwner->HasAuthority())
                    {
                        for (const FGameplayTag& AbilityTag : Allele.PhenotypeAbilities)
                        {
                            CharOwner->FindComponentByClass<UDynamicGASManagerComponent>()->TryLearnAndGrantAbility(ASC, AbilityTag, FGameplayTag::EmptyTag);
                        }
                    }
                    break;
                }
            }
        }
    }
}

FGameplayTag UDynamicLifeComponent::GetCurrentLifeStageTag() const
{
    ADynamicCharacter* CharOwner = Cast<ADynamicCharacter>(GetOwner());
    ADynamicGameState* GS = GetWorld() ? GetWorld()->GetGameState<ADynamicGameState>() : nullptr;
    if (!CharOwner || !GS) return FGameplayTag::EmptyTag;

    UDynamicCustomizationComponent* CustComp = CharOwner->GetCustomizationComponent();
    if (!CustComp) return FGameplayTag::EmptyTag;

    FCharacterTemplateDef Def;
    if (GS->GetCharacterTemplate(CustComp->ActiveCharacterTemplateTag, Def) && Def.AgingProfile.GrowthPhases.Num() > 0)
    {
        const float NormAge = GetNormalizedAge();
        FGameplayTag CurrentStage = Def.AgingProfile.GrowthPhases[0].LifeStageTag;

        for (const FContinuousGrowthKeyframe& Frame : Def.AgingProfile.GrowthPhases)
        {
            if (NormAge >= Frame.NormalizedAge && Frame.LifeStageTag.IsValid())
            {
                CurrentStage = Frame.LifeStageTag;
            }
        }
        return CurrentStage;
    }
    return FGameplayTag::EmptyTag;
}

bool UDynamicLifeComponent::IsCharacterAdult() const
{
    // Evaluates against a general adult tag or checks if the tag matches adult hierarchies
    FGameplayTag StageTag = GetCurrentLifeStageTag();
    static const FGameplayTag AdultRootTag = FGameplayTag::RequestGameplayTag(TEXT("Age.Stage.Adult"), false);
    
    return StageTag.IsValid() && (StageTag == AdultRootTag || StageTag.MatchesTag(AdultRootTag));
}