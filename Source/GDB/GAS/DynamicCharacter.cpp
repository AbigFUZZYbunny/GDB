// DynamicCharacter.cpp
#include "DynamicCharacter.h"
#include "AbilitySystemComponent.h"
#include "DynamicAttributeSet.h"
#include "DynamicGASManagerComponent.h"
#include "MoverComponent.h"

ADynamicCharacter::ADynamicCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	bIsRagdolling = false;

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
	RootComponent = CapsuleComponent;

	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);

	MoverComponent = CreateDefaultSubobject<UMoverComponent>(TEXT("MoverComponent"));
	PhysicsControlComponent = CreateDefaultSubobject<UPhysicsControlComponent>(TEXT("PhysicsControlComponent"));

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	DynamicGASManager = CreateDefaultSubobject<UDynamicGASManagerComponent>(TEXT("DynamicGASManager"));
	AttributeSet = CreateDefaultSubobject<UDynamicAttributeSet>(TEXT("DynamicAttributeSet"));
}

void ADynamicCharacter::BeginPlay()
{
	Super::BeginPlay();
}

UAbilitySystemComponent* ADynamicCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ADynamicCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializeAbilitySystem();
}

void ADynamicCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitializeAbilitySystem();
}

void ADynamicCharacter::InitializeAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		// FIX: Correctly calls the Fast Array replication sync handle on initialization
		if (const UDynamicAttributeSet* DynSet = Cast<UDynamicAttributeSet>(AttributeSet))
		{
			const_cast<UDynamicAttributeSet*>(DynSet)->OnRep_FastAttributes();
		}
	}
}

void ADynamicCharacter::StartRagdoll()
{
	if (bIsRagdolling) return;
	bIsRagdolling = true;

	if (MoverComponent) MoverComponent->Deactivate();

	if (MeshComponent && PhysicsControlComponent)
	{
		MeshComponent->SetCollisionProfileName(TEXT("Ragdoll"));
		MeshComponent->SetSimulatePhysics(true);
	}
}

void ADynamicCharacter::RecoverFromRagdoll()
{
	if (!bIsRagdolling) return;
	bIsRagdolling = false;

	if (MeshComponent)
	{
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->AttachToComponent(CapsuleComponent, FAttachmentTransformRules::KeepRelativeTransform);
		MeshComponent->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		MeshComponent->SetCollisionProfileName(TEXT("CharacterMesh"));
	}

	if (MoverComponent) MoverComponent->Activate(true);
}