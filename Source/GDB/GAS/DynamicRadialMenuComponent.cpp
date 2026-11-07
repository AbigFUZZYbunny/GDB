#include "DynamicRadialMenuComponent.h"
#include "DynamicGameState.h"
#include "DynamicGASManagerComponent.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicPhysicsActor.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"

UDynamicRadialMenuComponent::UDynamicRadialMenuComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UDynamicRadialMenuComponent::BeginPlay()
{
    Super::BeginPlay();

    AActor* OwnerActor = GetOwner();
    if (OwnerActor && OwnerActor->HasAuthority())
    {
        if (UWorld* World = GetWorld())
        {
            if (ADynamicGameState* GS = World->GetGameState<ADynamicGameState>())
            {
                ConfiguredMenus.Empty();
                for (const auto& Pair : GS->MenuMasterRegistry)
                {
                    const FDynamicMenuDef& Def = Pair.Value;
                    if (!Def.bIsDynamicRuntimeGathered && Def.MenuStyle == EDynamicMenuStyle::Radial)
                    {
                        FDynamicRadialConfig NewConfig;
                        NewConfig.MenuTag = Def.MenuTag;
                        NewConfig.Initialize(Def.PresetCount, Def.SlotCount);
                        ConfiguredMenus.Add(NewConfig);
                    }
                }
            }
        }
    }
}

void UDynamicRadialMenuComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UDynamicRadialMenuComponent, ConfiguredMenus);
}

bool UDynamicRadialMenuComponent::GetMenuConfig(FGameplayTag MenuTag, FDynamicRadialConfig& OutConfig) const
{
    if (const FDynamicRadialConfig* Found = FindMenuConfigPtr(MenuTag))
    {
        OutConfig = *Found;
        return true;
    }
    return false;
}

const FDynamicRadialConfig* UDynamicRadialMenuComponent::FindMenuConfigPtr(FGameplayTag MenuTag) const
{
    for (const FDynamicRadialConfig& Config : ConfiguredMenus)
    {
        if (Config.MenuTag == MenuTag) return &Config;
    }
    return nullptr;
}

FDynamicRadialConfig* UDynamicRadialMenuComponent::FindMenuConfigMutable(FGameplayTag MenuTag)
{
    for (FDynamicRadialConfig& Config : ConfiguredMenus)
    {
        if (Config.MenuTag == MenuTag) return &Config;
    }
    return nullptr;
}

void UDynamicRadialMenuComponent::SetSlotBinding(FGameplayTag MenuTag, int32 PresetIndex, int32 SlotIndex, FGameplayTag AssignedTag, FGuid ContextID)
{
    AActor* OwnerActor = GetOwner();
    if (!OwnerActor || !OwnerActor->HasAuthority()) return;

    FDynamicRadialConfig* TargetConfig = FindMenuConfigMutable(MenuTag);
    if (TargetConfig && TargetConfig->Presets.IsValidIndex(PresetIndex))
    {
        if (TargetConfig->Presets[PresetIndex].Slots.IsValidIndex(SlotIndex))
        {
            TargetConfig->Presets[PresetIndex].Slots[SlotIndex].AssignedTag = AssignedTag;
            TargetConfig->Presets[PresetIndex].Slots[SlotIndex].ContextID = ContextID;
        }
    }
}

void UDynamicRadialMenuComponent::CycleActivePreset(FGameplayTag MenuTag, bool bForward)
{
    FDynamicRadialConfig* TargetConfig = FindMenuConfigMutable(MenuTag);
    if (TargetConfig && TargetConfig->Presets.Num() > 0)
    {
        int32 NewIndex = TargetConfig->ActivePresetIndex + (bForward ? 1 : -1);
        if (NewIndex < 0) NewIndex = TargetConfig->Presets.Num() - 1;
        else if (NewIndex >= TargetConfig->Presets.Num()) NewIndex = 0;

        TargetConfig->ActivePresetIndex = NewIndex;
        OnRadialPresetChanged.Broadcast(MenuTag, NewIndex);

        Server_CycleActivePreset(MenuTag, NewIndex);
    }
}

void UDynamicRadialMenuComponent::Server_CycleActivePreset_Implementation(FGameplayTag MenuTag, int32 NewIndex)
{
    FDynamicRadialConfig* TargetConfig = FindMenuConfigMutable(MenuTag);
    if (TargetConfig && TargetConfig->Presets.IsValidIndex(NewIndex))
    {
        TargetConfig->ActivePresetIndex = NewIndex;
    }
}

int32 UDynamicRadialMenuComponent::GetSlotIndexFromInputVector(FVector2D InputDelta, int32 TotalSlots) const
{
    if (InputDelta.IsNearlyZero(0.15f) || TotalSlots <= 0) return -1;

    float Angle = FMath::RadiansToDegrees(FMath::Atan2(InputDelta.Y, InputDelta.X));
    if (Angle < 0.0f) Angle += 360.0f;

    const float SliceDegrees = 360.0f / static_cast<float>(TotalSlots);
    Angle += (SliceDegrees * 0.5f);
    if (Angle >= 360.0f) Angle -= 360.0f;

    return FMath::Clamp(FMath::FloorToInt(Angle / SliceDegrees), 0, TotalSlots - 1);
}

void UDynamicRadialMenuComponent::ExecuteRadialSlot(FGameplayTag MenuTag, int32 SlotIndex)
{
    if (SlotIndex < 0) return;

    if (const FRadialPreset* Gathered = TransientGatheredMenus.Find(MenuTag))
    {
        if (Gathered->Slots.IsValidIndex(SlotIndex))
        {
            ExecuteResolvedAction(MenuTag, Gathered->Slots[SlotIndex]);
            return;
        }
    }

    if (const FDynamicRadialConfig* Config = FindMenuConfigPtr(MenuTag))
    {
        Server_ExecuteRadialSlot(MenuTag, Config->ActivePresetIndex, SlotIndex);
    }
}

void UDynamicRadialMenuComponent::Server_ExecuteRadialSlot_Implementation(FGameplayTag MenuTag, int32 PresetIndex, int32 SlotIndex)
{
    if (const FDynamicRadialConfig* Config = FindMenuConfigPtr(MenuTag))
    {
        if (Config->Presets.IsValidIndex(PresetIndex))
        {
            if (Config->Presets[PresetIndex].Slots.IsValidIndex(SlotIndex))
            {
                ExecuteResolvedAction(MenuTag, Config->Presets[PresetIndex].Slots[SlotIndex]);
            }
        }
    }
}

void UDynamicRadialMenuComponent::ExecuteResolvedAction(FGameplayTag MenuTag, const FRadialSlotData& SlotData)
{
    if (!SlotData.AssignedTag.IsValid()) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    static const FGameplayTag AbilityPrefix = FGameplayTag::RequestGameplayTag(TEXT("Ability"), false);
    if (SlotData.AssignedTag.MatchesTag(AbilityPrefix))
    {
        if (UDynamicGASManagerComponent* GASManager = Owner->FindComponentByClass<UDynamicGASManagerComponent>())
        {
            UAbilitySystemComponent* ASC = Owner->FindComponentByClass<UAbilitySystemComponent>();
            GASManager->TriggerAbilityOrCombo(ASC, SlotData.AssignedTag);
        }
    }

    OnRadialActionTriggered.Broadcast(MenuTag, SlotData.AssignedTag, SlotData.ContextID);
}

void UDynamicRadialMenuComponent::GatherDynamicRadial(FGameplayTag MenuTag)
{
    AActor* Owner = GetOwner();
    UWorld* World = GetWorld();
    if (!Owner || !World) return;

    ADynamicGameState* GS = World->GetGameState<ADynamicGameState>();
    if (!GS) return;

    const FDynamicMenuDef* Def = GS->MenuMasterRegistry.Find(MenuTag);
    if (!Def) return;

    FRadialPreset GatheredPreset(Def->SlotCount);
    int32 CurrentSlot = 0;

    static const FGameplayTag ContainerMenuTag = FGameplayTag::RequestGameplayTag(TEXT("UI.Radial.Containers"), false);
    if (MenuTag.MatchesTag(ContainerMenuTag))
    {
        // 1. Equipped Wearable Containers (Backpacks, Satchels, Pouches)
        if (UDynamicEquipmentComponent* EquipComp = Owner->FindComponentByClass<UDynamicEquipmentComponent>())
        {
            for (const FDynamicEquipmentSlotItem& EquipSlot : EquipComp->ReplicatedEquipmentSlots.Items)
            {
                if (EquipSlot.EquippedItemArchetypeTag.IsValid() && CurrentSlot < Def->SlotCount)
                {
                    if (const FDynamicPhysicsActorDef* ItemDef = GS->MasterPhysicsActorRegistry.Find(EquipSlot.EquippedItemArchetypeTag))
                    {
                        if (ItemDef->Containers.Num() > 0)
                        {
                            // Form deterministic GUID from the unique SlotTag for equipped container slots
                            const uint32 SlotHash = GetTypeHash(EquipSlot.SlotTag);
                            const FGuid SlotContextGuid(SlotHash, static_cast<uint32>(EquipSlot.LinkedLimbId), 0, 0);

                            GatheredPreset.Slots[CurrentSlot] = FRadialSlotData(EquipSlot.EquippedItemArchetypeTag, SlotContextGuid);
                            CurrentSlot++;
                        }
                    }
                }
            }
        }

        const float Radius = Def->DynamicSearchRadius;
        FCollisionShape Sphere = FCollisionShape::MakeSphere(Radius);
        FCollisionObjectQueryParams ObjParams;
        ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
        ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);

        TArray<FOverlapResult> Overlaps;
        if (World->OverlapMultiByObjectType(Overlaps, Owner->GetActorLocation(), FQuat::Identity, ObjParams, Sphere))
        {
            for (const FOverlapResult& Overlap : Overlaps)
            {
                if (ADynamicPhysicsActor* PhysActor = Cast<ADynamicPhysicsActor>(Overlap.GetActor()))
                {
                    if (const FDynamicPhysicsActorDef* ActorDef = GS->MasterPhysicsActorRegistry.Find(PhysActor->GetActorArchetypeTag()))
                    {
                        if (ActorDef->Containers.Num() > 0 && CurrentSlot < Def->SlotCount)
                        {
                            GatheredPreset.Slots[CurrentSlot] = FRadialSlotData(PhysActor->GetActorArchetypeTag(), PhysActor->GetActorGuid());
                            CurrentSlot++;
                            if (CurrentSlot >= Def->SlotCount) break;
                        }
                    }
                }
            }
        }
    }

    TransientGatheredMenus.Add(MenuTag, GatheredPreset);
    OnDynamicRadialGathered.Broadcast(MenuTag, GatheredPreset);
}

bool UDynamicRadialMenuComponent::HandleRadialInputAction(FGameplayTag InputActionTag, bool bIsPressed)
{
    if (!InputActionTag.IsValid()) return false;

    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return false;

    for (const auto& Pair : GS->MenuMasterRegistry)
    {
        const FDynamicMenuDef& Def = Pair.Value;

        if (Def.OpenInputActionTag.MatchesTagExact(InputActionTag))
        {
            if (bIsPressed && Def.bIsDynamicRuntimeGathered)
            {
                GatherDynamicRadial(Def.MenuTag);
            }
            return true;
        }

        if (bIsPressed)
        {
            if (Def.CycleNextInputActionTag.MatchesTagExact(InputActionTag))
            {
                CycleActivePreset(Def.MenuTag, true);
                return true;
            }
            else if (Def.CyclePrevInputActionTag.MatchesTagExact(InputActionTag))
            {
                CycleActivePreset(Def.MenuTag, false);
                return true;
            }
        }
    }

    return false;
}

FString UDynamicRadialMenuComponent::ExtractMenuTagLeaf(FGameplayTag MenuTag)
{
    // Extracts the terminal leaf (e.g., "UI.List.Stats" -> "Stats", "UI.Tree.Masteries" -> "Masteries")
    FString TagStr = MenuTag.ToString();
    int32 LastDotIndex = INDEX_NONE;
    if (TagStr.FindLastChar(TEXT('.'), LastDotIndex))
    {
        return TagStr.RightChop(LastDotIndex + 1);
    }
    return TagStr;
}

FGameplayTag UDynamicRadialMenuComponent::ResolveMenuButtonAction(FGameplayTag MenuTag, const FString& ItemName) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FGameplayTag::EmptyTag;

    if (const FDynamicMenuDef* Def = GS->MenuMasterRegistry.Find(MenuTag))
    {
        const FString MenuTagLeaf = ExtractMenuTagLeaf(MenuTag);
        return Def->ResolveTokenizedActionTag(MenuTagLeaf, ItemName);
    }
    return FGameplayTag::EmptyTag;
}

FString UDynamicRadialMenuComponent::ResolveMenuTitle(FGameplayTag MenuTag, const FString& ItemName) const
{
    UWorld* World = GetWorld();
    ADynamicGameState* GS = World ? World->GetGameState<ADynamicGameState>() : nullptr;
    if (!GS) return FString();

    if (const FDynamicMenuDef* Def = GS->MenuMasterRegistry.Find(MenuTag))
    {
        static const FGameplayTag LocNameTag = FGameplayTag::RequestGameplayTag(TEXT("Localization.Name"), false);
        const FString MenuTagLeaf = ExtractMenuTagLeaf(MenuTag);
        return Def->ResolveLocalizedTitle(LocNameTag, MenuTagLeaf, ItemName);
    }
    return FString();
}