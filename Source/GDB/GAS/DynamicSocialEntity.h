// DynamicSocialEntity.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "DynamicGASTypes.h"
#include "DynamicSocialEntity.generated.h"

class ADynamicCharacter;

UCLASS()
class GDB_API ADynamicSocialEntity : public AActor
{
    GENERATED_BODY()

public:
    ADynamicSocialEntity();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic Entity")
    FGuid EntityGuid;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic Entity")
    EDynastyEntityType EntityType = EDynastyEntityType::FamilyHouse;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic Entity")
    FString EntityName = TEXT("");

    // Authoritative default language spoken within this institution, shop, or city enclave
    UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Dynamic Social Entity|Language")
    FGameplayTag DefaultLanguageTag;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "Dynamic Entity")
    FFastEntityMemberArray Members;

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Dynamic Entity")
    void AddMember(FGuid MemberGuid, const FString& DisplayName, FGameplayTag RoleTag, FGuid ParentGuid = FGuid());

    UFUNCTION(BlueprintPure, Category = "Dynamic Entity|Dynasty")
    TArray<FEntityMemberRecord> GetFamilyTreeMembers() const;

    UFUNCTION(BlueprintPure, Category = "Dynamic Social Entity|Language")
    FGameplayTag GetResolvedSpokenLanguage(ADynamicCharacter* InteractingSpeaker) const;
};