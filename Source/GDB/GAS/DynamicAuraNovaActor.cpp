#include "DynamicAuraNovaActor.h"
#include "Components/SphereComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicCharacter.h"
#include "DynamicAttributeSet.h"
#include "DynamicEffectFactory.h"
#include "DynamicGameplayEffectContext.h"

ADynamicAuraNovaActor::ADynamicAuraNovaActor()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;

    AreaSphere = CreateDefaultSubobject<USphereComponent>(TEXT("AreaSphere"));
    AreaSphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
    AreaSphere->SetGenerateOverlapEvents(true);
    RootComponent = AreaSphere;
}

void ADynamicAuraNovaActor::InitializeAsAura(UAbilitySystemComponent* InSourceASC, const FDynamicAuraProfile& InAuraProfile, const TArray<FSourcePassBackRule>& InPassBackRules)
{
    bIsNovaMode = false;
    SourceASC = InSourceASC;
    AuraProfile = InAuraProfile;
    PassBackRules = InPassBackRules;

    CurrentRadius = (AuraProfile.RadiusBehavior == EAuraRadiusBehavior::ExpandOut) ? AuraProfile.MinRadius : AuraProfile.MaxRadius;
    AreaSphere->SetSphereRadius(CurrentRadius);

    if (HasAuthority())
    {
        AreaSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ADynamicAuraNovaActor::OnSphereBeginOverlap);
        AreaSphere->OnComponentEndOverlap.AddUniqueDynamic(this, &ADynamicAuraNovaActor::OnSphereEndOverlap);
    }
    SetLifeSpan(AuraProfile.Duration);
}

void ADynamicAuraNovaActor::InitializeAsNova(UAbilitySystemComponent* InSourceASC, const FDynamicNovaProfile& InNovaProfile, const TArray<FSourcePassBackRule>& InPassBackRules)
{
    bIsNovaMode = true;
    SourceASC = InSourceASC;
    NovaProfile = InNovaProfile;
    PassBackRules = InPassBackRules;

    CurrentRadius = NovaProfile.StartRadius;
    AreaSphere->SetSphereRadius(CurrentRadius);

    if (HasAuthority())
    {
        AreaSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &ADynamicAuraNovaActor::OnSphereBeginOverlap);
    }
    SetLifeSpan(NovaProfile.ExpansionDuration + 0.1f);
}

void ADynamicAuraNovaActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    ElapsedTime += DeltaTime;

    if (bIsNovaMode) TickNova(DeltaTime);
    else TickAura(DeltaTime);
}

void ADynamicAuraNovaActor::TickAura(float DeltaTime)
{
    const float Alpha = FMath::Clamp(ElapsedTime / FMath::Max(0.01f, AuraProfile.Duration), 0.0f, 1.0f);

    switch (AuraProfile.RadiusBehavior)
    {
        case EAuraRadiusBehavior::StayFullSize:
            CurrentRadius = AuraProfile.MaxRadius;
            break;
        case EAuraRadiusBehavior::ExpandOut:
            CurrentRadius = FMath::Lerp(AuraProfile.MinRadius, AuraProfile.MaxRadius, Alpha);
            break;
        case EAuraRadiusBehavior::ContractIn:
            CurrentRadius = FMath::Lerp(AuraProfile.MaxRadius, AuraProfile.MinRadius, Alpha);
            break;
        case EAuraRadiusBehavior::ExpandAndContract:
            CurrentRadius = FMath::Lerp(AuraProfile.MinRadius, AuraProfile.MaxRadius, FMath::Abs(FMath::Sin(ElapsedTime * AuraProfile.CycleSpeed)));
            break;
        case EAuraRadiusBehavior::PulseAtFullSize:
            CurrentRadius = AuraProfile.MaxRadius * (0.9f + 0.1f * FMath::Sin(ElapsedTime * AuraProfile.CycleSpeed * 3.0f));
            break;
    }

    AreaSphere->SetSphereRadius(CurrentRadius, true);

    if (!HasAuthority()) return;

    if (AuraProfile.ApplicationType == EAuraEffectApplicationType::ConstantRate && AuraProfile.TargetAttributeTag.IsValid())
    {
        const float FrameDelta = AuraProfile.EffectMagnitude * DeltaTime;
        for (AActor* Target : ActiveTargets)
        {
            if (ADynamicCharacter* Char = Cast<ADynamicCharacter>(Target))
            {
                if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(Char->GetDynamicAttributeSet()))
                {
                    Attr->SetDynamicAttributeValue(AuraProfile.TargetAttributeTag, Attr->GetDynamicAttributeValue(AuraProfile.TargetAttributeTag) + FrameDelta);
                }
            }
        }
    }
    else if (AuraProfile.ApplicationType == EAuraEffectApplicationType::TickInterval)
    {
        TickAccumulator += DeltaTime;
        if (TickAccumulator >= AuraProfile.TickInterval)
        {
            TickAccumulator -= AuraProfile.TickInterval;
            for (AActor* Target : ActiveTargets)
            {
                if (ADynamicCharacter* Char = Cast<ADynamicCharacter>(Target))
                {
                    if (AuraProfile.TargetAttributeTag.IsValid())
                    {
                        if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(Char->GetDynamicAttributeSet()))
                        {
                            Attr->SetDynamicAttributeValue(AuraProfile.TargetAttributeTag, Attr->GetDynamicAttributeValue(AuraProfile.TargetAttributeTag) + AuraProfile.EffectMagnitude);
                        }
                    }
                    if (UAbilitySystemComponent* TargetASC = Char->GetAbilitySystemComponent())
                    {
                        for (const FGameplayTag& EffTag : AuraProfile.EffectsToApply)
                        {
                            FGameplayEffectSpecHandle Spec = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC.Get(), EffTag, 1);
                            if (Spec.IsValid()) SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
                        }
                    }
                }
            }
        }
    }
}

void ADynamicAuraNovaActor::TickNova(float DeltaTime)
{
    const float Alpha = FMath::Clamp(ElapsedTime / FMath::Max(0.01f, NovaProfile.ExpansionDuration), 0.0f, 1.0f);
    CurrentRadius = FMath::Lerp(NovaProfile.StartRadius, NovaProfile.EndRadius, Alpha);
    AreaSphere->SetSphereRadius(CurrentRadius, true);
}

void ADynamicAuraNovaActor::OnSphereBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
    if (!HasAuthority() || !OtherActor || OtherActor == GetInstigator()) return;

    ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(OtherActor);
    if (!TargetChar) return;

    if (bIsNovaMode)
    {
        if (ActiveTargets.Contains(OtherActor)) return;
        ActiveTargets.Add(OtherActor);

        ApplyPassBackToSource(EPassBackTriggerEvent::OnCollision, 0.0f);

        // Apply Nova outward physics force and carried effects
        const FVector OutwardNormal = (GetActorLocation() - OtherActor->GetActorLocation()).GetSafeNormal();
        TargetChar->ServerProcessAuthoritativeLimbHit(
            TargetChar,
            SweepResult.BoneName != NAME_None ? SweepResult.BoneName : FName("pelvis"),
            NovaProfile.KineticImpulseOnHit,
            1.0f,
            OtherActor->GetActorLocation(),
            OutwardNormal,
            GetInstigator());

        if (UAbilitySystemComponent* TargetASC = TargetChar->GetAbilitySystemComponent())
        {
            for (const FGameplayTag& EffTag : NovaProfile.EffectsToApplyOnHit)
            {
                FGameplayEffectSpecHandle Spec = UDynamicEffectFactory::CreateDynamicEffectSpec(SourceASC.Get(), EffTag, 1);
                if (Spec.IsValid())
                {
                    FDynamicGameplayEffectContext* DynCtx = static_cast<FDynamicGameplayEffectContext*>(Spec.Data->GetContext().Get());
                    if (DynCtx) DynCtx->SourcePassBackRules = PassBackRules;
                    SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
                }
            }
        }
    }
    else
    {
        ActiveTargets.Add(OtherActor);
        if (AuraProfile.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn && AuraProfile.TargetAttributeTag.IsValid())
        {
            if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(TargetChar->GetDynamicAttributeSet()))
            {
                Attr->SetDynamicAttributeValue(AuraProfile.TargetAttributeTag, Attr->GetDynamicAttributeValue(AuraProfile.TargetAttributeTag) + AuraProfile.EffectMagnitude);
            }
        }
    }
}

void ADynamicAuraNovaActor::OnSphereEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
    if (!HasAuthority() || bIsNovaMode || !OtherActor) return;

    if (ActiveTargets.Remove(OtherActor) > 0 && AuraProfile.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn && AuraProfile.TargetAttributeTag.IsValid())
    {
        if (ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(OtherActor))
        {
            if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(TargetChar->GetDynamicAttributeSet()))
            {
                Attr->SetDynamicAttributeValue(AuraProfile.TargetAttributeTag, Attr->GetDynamicAttributeValue(AuraProfile.TargetAttributeTag) - AuraProfile.EffectMagnitude);
            }
        }
    }
}

void ADynamicAuraNovaActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority() && !bIsNovaMode && AuraProfile.ApplicationType == EAuraEffectApplicationType::PassiveWhileIn && AuraProfile.TargetAttributeTag.IsValid())
    {
        for (AActor* Target : ActiveTargets)
        {
            if (ADynamicCharacter* TargetChar = Cast<ADynamicCharacter>(Target))
            {
                if (UDynamicAttributeSet* Attr = const_cast<UDynamicAttributeSet*>(TargetChar->GetDynamicAttributeSet()))
                {
                    Attr->SetDynamicAttributeValue(AuraProfile.TargetAttributeTag, Attr->GetDynamicAttributeValue(AuraProfile.TargetAttributeTag) - AuraProfile.EffectMagnitude);
                }
            }
        }
    }
    Super::EndPlay(EndPlayReason);
}

void ADynamicAuraNovaActor::ApplyPassBackToSource(EPassBackTriggerEvent EventType, float DamageAmount)
{
    if (!SourceASC.IsValid()) return;
    const UDynamicAttributeSet* SourceAttr = Cast<UDynamicAttributeSet>(SourceASC->GetAttributeSet(UDynamicAttributeSet::StaticClass()));
    if (!SourceAttr) return;

    for (const FSourcePassBackRule& Rule : PassBackRules)
    {
        if (Rule.TriggerEvent == EventType && Rule.SourceAttributeTag.IsValid())
        {
            float Grant = Rule.BaseGrantAmount + (Rule.bScaleWithDamageDealt ? (DamageAmount * Rule.DamageRatioScalar) : 0.0f);
            float Cur = SourceAttr->GetDynamicAttributeValue(Rule.SourceAttributeTag);
            const_cast<UDynamicAttributeSet*>(SourceAttr)->SetDynamicAttributeValue(Rule.SourceAttributeTag, Cur + Grant);
        }
    }
}