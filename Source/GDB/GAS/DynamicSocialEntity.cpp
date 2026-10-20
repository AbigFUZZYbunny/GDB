// DynamicSocialEntity.cpp
#include "DynamicSocialEntity.h"
#include "Net/UnrealNetwork.h"

ADynamicSocialEntity::ADynamicSocialEntity()
{
    PrimaryActorTick.bCanEverTick = false; // Fixed: AActor uses PrimaryActorTick instead of PrimaryComponentTick
    bReplicates = true;
    SetReplicateMovement(false);

    EntityGuid = FGuid::NewGuid();
    DefaultLanguageTag = FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
}

void ADynamicSocialEntity::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADynamicSocialEntity, EntityGuid);
    DOREPLIFETIME(ADynamicSocialEntity, EntityType);
    DOREPLIFETIME(ADynamicSocialEntity, EntityName);
    DOREPLIFETIME(ADynamicSocialEntity, DefaultLanguageTag);
    DOREPLIFETIME(ADynamicSocialEntity, Members);
}

void ADynamicSocialEntity::AddMember(FGuid MemberGuid, const FString& DisplayName, FGameplayTag RoleTag, FGuid ParentGuid)
{
    if (!HasAuthority() || !MemberGuid.IsValid()) return;

    for (FEntityMemberRecord& Record : Members.Items)
    {
        if (Record.MemberGuid == MemberGuid)
        {
            Record.MemberDisplayName = DisplayName;
            Record.EntityRoleTag = RoleTag;
            Record.ParentMemberGuid = ParentGuid;
            Members.MarkItemDirty(Record);
            return;
        }
    }

    FEntityMemberRecord NewRecord;
    NewRecord.MemberGuid = MemberGuid;
    NewRecord.MemberDisplayName = DisplayName;
    NewRecord.EntityRoleTag = RoleTag;
    NewRecord.ParentMemberGuid = ParentGuid;

    FEntityMemberRecord& Added = Members.Items.Add_GetRef(NewRecord);
    Members.MarkItemDirty(Added);
}

TArray<FEntityMemberRecord> ADynamicSocialEntity::GetFamilyTreeMembers() const
{
    return Members.Items;
}

FGameplayTag ADynamicSocialEntity::GetResolvedSpokenLanguage(ADynamicCharacter* InteractingSpeaker) const
{
    if (DefaultLanguageTag.IsValid())
    {
        return DefaultLanguageTag;
    }
    return FGameplayTag::RequestGameplayTag(TEXT("Language.Common"), false);
}