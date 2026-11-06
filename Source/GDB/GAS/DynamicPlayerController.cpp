#include "DynamicPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "DynamicInputSubsystem.h"
#include "DynamicCharacter.h"
#include "DynamicEquipmentComponent.h"
#include "DynamicContainerComponent.h"
#include "DynamicPhysicsActor.h"

void ADynamicPlayerController::BeginPlay()
{
    Super::BeginPlay();

    if (ULocalPlayer* LP = GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            if (UDynamicInputSubsystem* InputSub = LP->GetSubsystem<UDynamicInputSubsystem>())
            {
                if (UInputMappingContext* DynamicIMC = InputSub->GetDynamicMappingContext())
                {
                    Subsystem->AddMappingContext(DynamicIMC, 0);
                }
            }
        }
    }
}

void ADynamicPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
    {
        if (ULocalPlayer* LP = GetLocalPlayer())
        {
            if (UDynamicInputSubsystem* InputSub = LP->GetSubsystem<UDynamicInputSubsystem>())
            {
                for (const auto& Pair : InputSub->GetGeneratedActions())
                {
                    const FGameplayTag ActionTag = Pair.Key;
                    UInputAction* Action = Pair.Value;
                    if (!Action) continue;

                    // Bind directly to member functions using trailing parameters
                    EnhancedInput->BindAction(Action, ETriggerEvent::Started, this, &ADynamicPlayerController::HandleInputStarted, ActionTag);
                    EnhancedInput->BindAction(Action, ETriggerEvent::Triggered, this, &ADynamicPlayerController::HandleInputTriggered, ActionTag);
                    EnhancedInput->BindAction(Action, ETriggerEvent::Completed, this, &ADynamicPlayerController::HandleInputCompleted, ActionTag);
                }
            }
        }
    }
}

void ADynamicPlayerController::HandleInputStarted(const FInputActionValue& Value, FGameplayTag ActionTag)
{
    static const FGameplayTag WheelRootTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.UI.Wheel"), false);
    if (WheelRootTag.IsValid() && ActionTag.MatchesTag(WheelRootTag))
    {
        ActiveOpenWheelTag = ActionTag;
    }
    static const FGameplayTag TogglePerspectiveTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.Camera.TogglePerspective"), false);
    if (TogglePerspectiveTag.IsValid() && ActionTag.MatchesTag(TogglePerspectiveTag))
    {
        if (ADynamicCharacter* Char = Cast<ADynamicCharacter>(GetPawn()))
        {
            Char->TogglePerspective();
        }
    }
}

void ADynamicPlayerController::HandleInputTriggered(const FInputActionValue& Value, FGameplayTag ActionTag)
{
    ADynamicCharacter* Char = Cast<ADynamicCharacter>(GetPawn());
    if (!Char) return;

    static const FGameplayTag MovementWASDTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.Movement.WASD"), false);
    static const FGameplayTag WheelCycleTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.UI.Wheel.Cycle"), false);
    static const FGameplayTag WheelConfirmTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.UI.Wheel.SelectConfirm"), false);
    static const FGameplayTag QuickUseRootTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.UI.QuickUse"), false);
    static const FGameplayTag CameraLookTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.Camera.Look"), false);
    static const FGameplayTag CameraZoomTag = FGameplayTag::RequestGameplayTag(TEXT("InputAction.Camera.Zoom"), false);

    if (MovementWASDTag.IsValid() && ActionTag.MatchesTag(MovementWASDTag))
    {
        const FVector2D MoveVector = Value.Get<FVector2D>();
        Char->AddMovementInput(Char->GetActorForwardVector(), MoveVector.Y);
        Char->AddMovementInput(Char->GetActorRightVector(), MoveVector.X);
    }
    else if (CameraLookTag.IsValid() && ActionTag.MatchesTag(CameraLookTag))
    {
        if (!ActiveOpenWheelTag.IsValid()) // Lock look if a wheel is open
        {
            const FVector2D LookVector = Value.Get<FVector2D>();
            AddYawInput(LookVector.X);
            AddPitchInput(LookVector.Y);
        }
    }
    else if (CameraZoomTag.IsValid() && ActionTag.MatchesTag(CameraZoomTag))
    {
        if (!ActiveOpenWheelTag.IsValid()) 
        {
            Char->ZoomCamera(Value.Get<float>());
        }
    }
    else if (WheelCycleTag.IsValid() && ActionTag.MatchesTag(WheelCycleTag))
    {
        if (ActiveOpenWheelTag.IsValid())
        {
            const float ScrollDelta = Value.Get<float>();
            // UI wheel cycling hook
        }
    }
    else if (WheelConfirmTag.IsValid() && ActionTag.MatchesTag(WheelConfirmTag))
    {
        if (ActiveOpenWheelTag.IsValid())
        {
            if (QuickUseRootTag.IsValid() && ActiveOpenWheelTag.MatchesTag(QuickUseRootTag))
            {
                FGameplayTag HighlightedArchetype; // Provided by active wheel UI state
                if (HighlightedArchetype.IsValid())
                {
                    Server_ExecuteQuickUseEquip(HighlightedArchetype);
                }
            }
            ActiveOpenWheelTag = FGameplayTag::EmptyTag;
        }
    }
}

void ADynamicPlayerController::HandleInputCompleted(const FInputActionValue& Value, FGameplayTag ActionTag)
{
    if (ActiveOpenWheelTag.IsValid() && ActionTag == ActiveOpenWheelTag)
    {
        ActiveOpenWheelTag = FGameplayTag::EmptyTag;
    }
}

bool ADynamicPlayerController::Server_ExecuteQuickUseEquip_Validate(FGameplayTag ItemToEquipTag)
{
    return true;
}

void ADynamicPlayerController::Server_ExecuteQuickUseEquip_Implementation(FGameplayTag ItemToEquipTag)
{
    ADynamicCharacter* Char = Cast<ADynamicCharacter>(GetPawn());
    if (!Char || !ItemToEquipTag.IsValid()) return;

    UDynamicEquipmentComponent* EquipComp = Char->FindComponentByClass<UDynamicEquipmentComponent>();
    UDynamicContainerComponent* MainInv = Char->FindComponentByClass<UDynamicContainerComponent>();
    if (!EquipComp || !MainInv) return;

    static const FGameplayTag MainHandTag = FGameplayTag::RequestGameplayTag(TEXT("Slot.Hand.Right.Main"), false);
    const FGameplayTag EquippedTag = EquipComp->GetEquippedItemInSlot(MainHandTag);

    if (EquippedTag.IsValid())
    {
        if (ADynamicPhysicsActor* UnequippedActor = EquipComp->ServerUnequipSlot(MainHandTag, Char->GetActorTransform()))
        {
            bool bStashed = false;
            TArray<UDynamicContainerComponent*> AllContainers;
            Char->GetComponents<UDynamicContainerComponent>(AllContainers);

            for (UDynamicContainerComponent* Cont : AllContainers)
            {
                if (Cont->ServerStorePhysicalActor(UnequippedActor, Char))
                {
                    bStashed = true;
                    break;
                }
            }

            if (!bStashed)
            {
                if (UPrimitiveComponent* Prim = UnequippedActor->GetActivePhysicsPrimitive())
                {
                    Prim->SetSimulatePhysics(true);
                }
            }
        }
    }

    TArray<UDynamicContainerComponent*> AvailableContainers;
    Char->GetComponents<UDynamicContainerComponent>(AvailableContainers);

    for (UDynamicContainerComponent* Cont : AvailableContainers)
    {
        for (const FDynamicContainerSlotItem& Slot : Cont->GetReplicatedSlots().Items)
        {
            if (Slot.StoredActorArchetypeTag == ItemToEquipTag && Slot.StackCount > 0)
            {
                if (ADynamicPhysicsActor* Extracted = Cont->ServerExtractActorToHands(Slot.GridX, Slot.GridY, Char, FName("hand_r")))
                {
                    EquipComp->ServerEquipItem(Extracted);
                    return;
                }
            }
        }
    }
}