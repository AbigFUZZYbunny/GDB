// DynamicLifeComponent.cpp
#include "DynamicLifeComponent.h"
#include "DynamicGameState.h"
#include "DynamicCharacter.h"
#include "DynamicCustomizationComponent.h"
#include "DynamicAttributeSet.h"
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
        
        // Evaluate smooth growth once every in-game day (or an optimized interval)
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
    
    // Corrected to access CalendarConfig directly
    const float GameSecs = ElapsedRealSecs * GS->CalendarConfig.TimeScalar; 
    const float SecsPerDay = GS->CalendarConfig.HoursPerDay * 3600.0f;
    const float DaysPerYear = GS->CalendarConfig.DaysPerSeason * GS->CalendarConfig.SeasonsPerYear;
    
    return GameSecs / (SecsPerDay * DaysPerYear);
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

    // Find bounding keyframes
    FContinuousGrowthKeyframe FrameA = Def.AgingProfile.GrowthPhases[0];
    FContinuousGrowthKeyframe FrameB = Def.AgingProfile.GrowthPhases.Last();
    float LerpAlpha = 0.0f;

    for (int32 i = 0; i < Def.AgingProfile.GrowthPhases.Num() - 1; ++i)
    {
        if (NormalizedAge >= Def.AgingProfile.GrowthPhases[i].NormalizedAge && NormalizedAge <= Def.AgingProfile.GrowthPhases[i+1].NormalizedAge)
        {
            FrameA = Def.AgingProfile.GrowthPhases[i];
            FrameB = Def.AgingProfile.GrowthPhases[i+1];
            float Range = FrameB.NormalizedAge - FrameA.NormalizedAge;
            LerpAlpha = (Range > 0.0f) ? (NormalizedAge - FrameA.NormalizedAge) / Range : 0.0f;
            break;
        }
    }

    // 1. Lerp Physical Body Scale
    float TargetScale = FMath::Lerp(FrameA.ScaleMultiplier, FrameB.ScaleMultiplier, LerpAlpha);
    CharOwner->GetMeshComponent()->SetRelativeScale3D(FVector(TargetScale));

    // 2. Set Locomotion State (Assigned to the current FrameA threshold)
    if (CharOwner->HasAuthority() && CurrentLocomotionAnimTag != FrameA.LocomotionAnimTag)
    {
        CurrentLocomotionAnimTag = FrameA.LocomotionAnimTag;
    }

    // 3. Apply Morphs (Aging, Posture)
    for (const FTagFloatPair& MorphA : FrameA.MorphTargetOffsets)
    {
        float ValB = MorphA.Value;
        for (const FTagFloatPair& Mb : FrameB.MorphTargetOffsets) { if (Mb.KeyTag == MorphA.KeyTag) { ValB = Mb.Value; break; } }
        float BlendedMorph = FMath::Lerp(MorphA.Value, ValB, LerpAlpha);
        
        // Additive offset combined with baseline Bodyslide value
        float BaseSlide = CustComp->GetMorphSliderValue(MorphA.KeyTag);
        CustComp->ServerSetMorphSliderValue(MorphA.KeyTag, BaseSlide + BlendedMorph);
    }

    // 4. Lerp Physics Motors & Growth Attributes
    float TargetMotorMulti = FMath::Lerp(FrameA.PhysicsMotorMultiplier, FrameB.PhysicsMotorMultiplier, LerpAlpha);
    
    for (const FTagFloatPair& StatA : FrameA.StatModifiers)
    {
        float ValB = StatA.Value;
        for (const FTagFloatPair& Sb : FrameB.StatModifiers) { if (Sb.KeyTag == StatA.KeyTag) { ValB = Sb.Value; break; } }
        float BlendedStat = FMath::Lerp(StatA.Value, ValB, LerpAlpha);
        
        // Example: Base stat + Age modifier. (In a full system, you store Base vs AgeMod separately).
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

    // Check Template Overrides first (e.g. Elf overrides blonde dominance)
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

    // Fallback to global defaults if no override
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
        
        for (const FDynamicGenePairItem& MomGene : Mother->ReplicatedGenome.Items) {
            if (MomGene.LocusTag == TargetLocus) { MomContributed = FMath::RandBool() ? MomGene.MaternalAllele : MomGene.PaternalAllele; break; }
        }
        for (const FDynamicGenePairItem& DadGene : Father->ReplicatedGenome.Items) {
            if (DadGene.LocusTag == TargetLocus) { DadContributed = FMath::RandBool() ? DadGene.MaternalAllele : DadGene.PaternalAllele; break; }
        }

        if (FMath::FRand() <= LocusDef.MutationChance && LocusDef.PossibleAlleles.Num() > 0)
        {
            int32 MutIdx = FMath::RandRange(0, LocusDef.PossibleAlleles.Num() - 1);
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
    // Applies color overrides to CustComp and grants PhenotypeAbilities via ASC
}