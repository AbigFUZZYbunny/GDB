// DynamicInteractableComponent.cpp
#include "DynamicInteractableComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "AbilitySystemComponent.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicLogicEvaluator.h"

UDynamicInteractableComponent::UDynamicInteractableComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
    ReplicatedStateTags.OwningInteractable = this;
}

void UDynamicInteractableComponent::BeginPlay()
{
    Super::BeginPlay();
    ReplicatedStateTags.OwningInteractable = this;

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        for (const FGameplayTag& Tag : StartupStateTags)
        {
            AddStateTag(Tag);
        }
    }
}

void UDynamicInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicInteractableComponent, ReplicatedStateTags);
}

void UDynamicInteractableComponent::OnRep_ReplicatedStateTags()
{
    LocalStateCache.Reset();
    for (const FDynamicInteractableStateItem& Item : ReplicatedStateTags.Items)
    {
        if (Item.StateTag.IsValid())
        {
            LocalStateCache.AddTag(Item.StateTag);
        }
    }
    OnInteractionStateChanged.Broadcast(nullptr, FGameplayTag::EmptyTag, LocalStateCache);
}

void FDynamicInteractableStateItem::PreReplicatedRemove(const FFastInteractableStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningInteractable)
    {
        InArrayOwner.OwningInteractable->OnRep_ReplicatedStateTags();
    }
}

void FDynamicInteractableStateItem::PostReplicatedAdd(const FFastInteractableStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningInteractable)
    {
        InArrayOwner.OwningInteractable->OnRep_ReplicatedStateTags();
    }
}

void FDynamicInteractableStateItem::PostReplicatedChange(const FFastInteractableStateArray& InArrayOwner)
{
    if (InArrayOwner.OwningInteractable)
    {
        InArrayOwner.OwningInteractable->OnRep_ReplicatedStateTags();
    }
}

void UDynamicInteractableComponent::AddStateTag(FGameplayTag TagToAdd)
{
    if (!TagToAdd.IsValid() || LocalStateCache.HasTagExact(TagToAdd)) return;
    LocalStateCache.AddTag(TagToAdd);

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        FDynamicInteractableStateItem NewItem;
        NewItem.StateTag = TagToAdd;
        FDynamicInteractableStateItem& AddedRef = ReplicatedStateTags.Items.Add_GetRef(NewItem);
        ReplicatedStateTags.MarkItemDirty(AddedRef);
    }
}

void UDynamicInteractableComponent::RemoveStateTag(FGameplayTag TagToRemove)
{
    if (!TagToRemove.IsValid() || !LocalStateCache.HasTagExact(TagToRemove)) return;
    LocalStateCache.RemoveTag(TagToRemove);

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        int32 RemovedCount = ReplicatedStateTags.Items.RemoveAll([TagToRemove](const FDynamicInteractableStateItem& Item) {
            return Item.StateTag == TagToRemove;
        });
        if (RemovedCount > 0)
        {
            ReplicatedStateTags.MarkArrayDirty();
        }
    }
}

bool UDynamicInteractableComponent::ResolveKinematicTargetTransform(FGameplayTag TargetPointTag, FTransform& OutWorldTransform, USceneComponent*& OutTargetComponent) const
{
    OutTargetComponent = nullptr;
    AActor* OwnerActor = GetOwner();
    if (!IsValid(OwnerActor)) return false;

    OutWorldTransform = OwnerActor->GetActorTransform();
    OutTargetComponent = OwnerActor->GetRootComponent();

    if (!TargetPointTag.IsValid()) return false;

    for (const FInteractionTargetPoint& Point : KinematicTargetPoints)
    {
        if (Point.TargetPointTag == TargetPointTag)
        {
            AActor* BaseActor = Point.LinkedTargetActor.IsValid() ? Point.LinkedTargetActor.Get() : OwnerActor;
            if (!IsValid(BaseActor) || BaseActor->IsActorBeingDestroyed()) return false;

            // OPTIMIZATION: Check transient cached pointer first to bypass TInlineComponentArray and FName iterations
            if (Point.CachedResolvedComponent.IsValid())
            {
                USceneComponent* CachedComp = Point.CachedResolvedComponent.Get();
                OutTargetComponent = CachedComp;
                const FTransform BaseTransform = (!Point.BoneOrSocketName.IsNone() && CachedComp->DoesSocketExist(Point.BoneOrSocketName))
                    ? CachedComp->GetSocketTransform(Point.BoneOrSocketName, RTS_World)
                    : CachedComp->GetComponentTransform();
                OutWorldTransform = Point.LocalOffset * BaseTransform;
                return true;
            }

            // Fallback for first resolution: resolve component once, then cache
            USceneComponent* ResolvedComp = BaseActor->GetRootComponent();
            if (!Point.ComponentName.IsNone())
            {
                TInlineComponentArray<USceneComponent*> SceneComps(BaseActor);
                for (USceneComponent* Comp : SceneComps)
                {
                    if (Comp && Comp->GetFName() == Point.ComponentName)
                    {
                        ResolvedComp = Comp;
                        break;
                    }
                }
            }

            if (ResolvedComp)
            {
                Point.CachedResolvedComponent = ResolvedComp; // Cache for all subsequent interactions
                OutTargetComponent = ResolvedComp;
                const FTransform BaseTransform = (!Point.BoneOrSocketName.IsNone() && ResolvedComp->DoesSocketExist(Point.BoneOrSocketName))
                    ? ResolvedComp->GetSocketTransform(Point.BoneOrSocketName, RTS_World)
                    : ResolvedComp->GetComponentTransform();
                OutWorldTransform = Point.LocalOffset * BaseTransform;
                return true;
            }
        }
    }

    return false;
}

bool UDynamicInteractableComponent::CanAcceptInteraction(const FDynamicInteractionProfile& Profile, const AActor* QueryInstigator) const
{
    if (Profile.RequiredTargetTags.Num() > 0 && !LocalStateCache.HasAll(Profile.RequiredTargetTags))
    {
        return false;
    }

    if (Profile.BlockedTargetTags.Num() > 0 && LocalStateCache.HasAny(Profile.BlockedTargetTags))
    {
        return false;
    }

    if (!Profile.TargetValidationConditionTree.IsNone() && QueryInstigator)
    {
        if (const FDynamicConditionTreeContainer* ConditionTree = UDynamicGASManagerComponent::GetConditionTreeByName(QueryInstigator, Profile.TargetValidationConditionTree))
        {
            const UAbilitySystemComponent* SourceASC = QueryInstigator->FindComponentByClass<UAbilitySystemComponent>();
            const UAbilitySystemComponent* TargetASC = GetOwner() ? GetOwner()->FindComponentByClass<UAbilitySystemComponent>() : nullptr;

            FGameplayTagContainer SourceTags;
            if (SourceASC) SourceASC->GetOwnedGameplayTags(SourceTags);

            if (!UDynamicLogicEvaluator::EvaluateConditionTree(*ConditionTree, SourceASC, TargetASC, SourceTags, LocalStateCache))
            {
                return false;
            }
        }
    }

    return true;
}

bool UDynamicInteractableComponent::ServerExecuteInteraction(AActor* InstigatorActor, const FDynamicInteractionProfile& Profile, float ChargeAlpha)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority() || !CanAcceptInteraction(Profile, InstigatorActor))
    {
        return false;
    }

    for (const FGameplayTag& TagToRemove : Profile.TagsToRemoveFromTarget)
    {
        RemoveStateTag(TagToRemove);
    }
    for (const FGameplayTag& TagToAdd : Profile.TagsToAddToTarget)
    {
        AddStateTag(TagToAdd);
    }

    if (Profile.PhysicsMode != EInteractionPhysicsMode::None && InstigatorActor)
    {
        FTransform TargetPointTransform = OwnerActor->GetActorTransform();
        USceneComponent* TargetSceneComp = OwnerActor->GetRootComponent();
        ResolveKinematicTargetTransform(Profile.KinematicTargetTag, TargetPointTransform, TargetSceneComp);

        UPrimitiveComponent* TargetPrim = Cast<UPrimitiveComponent>(TargetSceneComp);
        if (!TargetPrim) TargetPrim = Cast<UPrimitiveComponent>(OwnerActor->GetRootComponent());

        if (TargetPrim && TargetPrim->IsSimulatingPhysics())
        {
            const float ForceMag = (Profile.ForceMetricTag.IsValid()
        ? Profile.InteractionMetrics.LocalLookupMap.FindRef(Profile.ForceMetricTag)
        : 0.0f) * ChargeAlpha;
            const FVector DirFromCaster = (TargetPointTransform.GetLocation() - InstigatorActor->GetActorLocation()).GetSafeNormal();

            switch (Profile.PhysicsMode)
            {
                case EInteractionPhysicsMode::PushAwayFromCaster:
                    TargetPrim->AddImpulseAtLocation(DirFromCaster * ForceMag, TargetPointTransform.GetLocation());
                    break;
                case EInteractionPhysicsMode::PullTowardCaster:
                    TargetPrim->AddImpulseAtLocation((-DirFromCaster) * ForceMag, TargetPointTransform.GetLocation());
                    break;
                case EInteractionPhysicsMode::AngularHingeTorque:
                    TargetPrim->AddAngularImpulseInRadians(FVector::UpVector * ForceMag);
                    break;
                default:
                    break;
            }
        }
    }

    OnInteractionStateChanged.Broadcast(InstigatorActor, Profile.InteractionActionTag, LocalStateCache);
    return true;
}

void UDynamicInteractableComponent::InvalidateAllKinematicCaches()
{
    for (FInteractionTargetPoint& Point : KinematicTargetPoints)
    {
        Point.InvalidateCache();
    }
}

void UDynamicInteractableComponent::ResetForPooling()
{
    LocalStateCache.Reset();
    ReplicatedStateTags.Items.Empty();
    
    // Invalidate cached scene component weak pointers
    for (FInteractionTargetPoint& TargetPoint : KinematicTargetPoints)
    {
        TargetPoint.InvalidateCache();
    }

    // Re-apply baseline startup state tags
    for (const FGameplayTag& Tag : StartupStateTags)
    {
        AddStateTag(Tag);
    }
    ReplicatedStateTags.MarkArrayDirty();
}