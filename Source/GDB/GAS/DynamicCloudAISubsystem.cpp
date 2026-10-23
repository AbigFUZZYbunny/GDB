// DynamicCloudAISubsystem.cpp
#include "DynamicCloudAISubsystem.h"
#include "DynamicCharacter.h"
#include "DynamicSocialComponent.h"
#include "GameplayTagsManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "JsonObjectConverter.h"

static void CollectAllGameplayTagChildrenRecursively(const UGameplayTagsManager& TagManager, const FGameplayTag& RootTag, FGameplayTagContainer& OutContainer)
{
    if (!RootTag.IsValid()) return;

    const FGameplayTagContainer DirectChildren = TagManager.RequestGameplayTagChildren(RootTag);
    for (const FGameplayTag& ChildTag : DirectChildren)
    {
        OutContainer.AddTag(ChildTag);
        CollectAllGameplayTagChildrenRecursively(TagManager, ChildTag, OutContainer);
    }
}

void UDynamicCloudAISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    ActiveBackend = ECloudAIExecutionBackend::LocalRuleBasedFallback;
    BuildAndCacheJsonSchema();
}

void UDynamicCloudAISubsystem::Deinitialize()
{
    CachedCloudAISchema.Reset();
    ActiveDirectCallbacks.Empty();
    Super::Deinitialize();
}

FString UDynamicCloudAISubsystem::SanitizeMarkdownJson(const FString& RawResponse)
{
    FString Trimmed = RawResponse.TrimStartAndEnd();

    if (Trimmed.StartsWith(TEXT("```json")))
    {
        Trimmed.RemoveFromStart(TEXT("```json"));
    }
    else if (Trimmed.StartsWith(TEXT("```")))
    {
        Trimmed.RemoveFromStart(TEXT("```"));
    }

    if (Trimmed.EndsWith(TEXT("```")))
    {
        Trimmed.RemoveFromEnd(TEXT("```"));
    }

    return Trimmed.TrimStartAndEnd();
}

void UDynamicCloudAISubsystem::BuildAndCacheJsonSchema()
{
    TSharedRef<FJsonObject> SchemaObj = MakeShareable(new FJsonObject());
    SchemaObj->SetStringField(TEXT("type"), TEXT("object"));

    TSharedPtr<FJsonObject> PropertiesObj = MakeShareable(new FJsonObject());
    TSharedPtr<FJsonObject> CommandsObj = MakeShareable(new FJsonObject());
    CommandsObj->SetStringField(TEXT("type"), TEXT("array"));

    TSharedPtr<FJsonObject> ItemsObj = MakeShareable(new FJsonObject());
    ItemsObj->SetStringField(TEXT("type"), TEXT("object"));

    TSharedPtr<FJsonObject> ItemPropsObj = MakeShareable(new FJsonObject());

    TSharedPtr<FJsonObject> ActionTagObj = MakeShareable(new FJsonObject());
    ActionTagObj->SetStringField(TEXT("type"), TEXT("string"));

    TArray<TSharedPtr<FJsonValue>> ValidActionTagsEnum;
    FGameplayTagContainer AllValidActionTags;
    const UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();

    static const FGameplayTag RootAction = FGameplayTag::RequestGameplayTag(TEXT("Action"), false);
    static const FGameplayTag RootSchedule = FGameplayTag::RequestGameplayTag(TEXT("Schedule"), false);
    static const FGameplayTag RootSocial = FGameplayTag::RequestGameplayTag(TEXT("Social"), false);
    static const FGameplayTag RootAbility = FGameplayTag::RequestGameplayTag(TEXT("Ability"), false);

    if (RootAction.IsValid())
    {
        AllValidActionTags.AddTag(RootAction);
        CollectAllGameplayTagChildrenRecursively(TagManager, RootAction, AllValidActionTags);
    }
    if (RootSchedule.IsValid())
    {
        AllValidActionTags.AddTag(RootSchedule);
        CollectAllGameplayTagChildrenRecursively(TagManager, RootSchedule, AllValidActionTags);
    }
    if (RootSocial.IsValid())
    {
        AllValidActionTags.AddTag(RootSocial);
        CollectAllGameplayTagChildrenRecursively(TagManager, RootSocial, AllValidActionTags);
    }
    if (RootAbility.IsValid())
    {
        AllValidActionTags.AddTag(RootAbility);
        CollectAllGameplayTagChildrenRecursively(TagManager, RootAbility, AllValidActionTags);
    }

    for (const FGameplayTag& Tag : AllValidActionTags)
    {
        ValidActionTagsEnum.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
    }
    ActionTagObj->SetArrayField(TEXT("enum"), ValidActionTagsEnum);
    ItemPropsObj->SetObjectField(TEXT("CommandActionTag"), ActionTagObj);

    TSharedPtr<FJsonObject> TargetTagObj = MakeShareable(new FJsonObject());
    TargetTagObj->SetStringField(TEXT("type"), TEXT("string"));

    TArray<TSharedPtr<FJsonValue>> ValidRoleTagsEnum;
    FGameplayTagContainer AllRoleTags;
    static const FGameplayTag RootRole = FGameplayTag::RequestGameplayTag(TEXT("Role"), false);
    if (RootRole.IsValid())
    {
        AllRoleTags.AddTag(RootRole);
        CollectAllGameplayTagChildrenRecursively(TagManager, RootRole, AllRoleTags);
    }

    for (const FGameplayTag& Tag : AllRoleTags)
    {
        ValidRoleTagsEnum.Add(MakeShareable(new FJsonValueString(Tag.ToString())));
    }
    ValidRoleTagsEnum.Add(MakeShareable(new FJsonValueString(TEXT(""))));
    TargetTagObj->SetArrayField(TEXT("enum"), ValidRoleTagsEnum);
    ItemPropsObj->SetObjectField(TEXT("TargetEntityOrPlaceRoleTag"), TargetTagObj);

    TSharedPtr<FJsonObject> UtteranceObj = MakeShareable(new FJsonObject());
    UtteranceObj->SetStringField(TEXT("type"), TEXT("string"));
    ItemPropsObj->SetObjectField(TEXT("UtteranceText"), UtteranceObj);

    TSharedPtr<FJsonObject> PriorityObj = MakeShareable(new FJsonObject());
    PriorityObj->SetStringField(TEXT("type"), TEXT("number"));
    ItemPropsObj->SetObjectField(TEXT("PriorityWeight"), PriorityObj);

    ItemsObj->SetObjectField(TEXT("properties"), ItemPropsObj);

    TArray<TSharedPtr<FJsonValue>> RequiredArray;
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("CommandActionTag"))));
    RequiredArray.Add(MakeShareable(new FJsonValueString(TEXT("PriorityWeight"))));
    ItemsObj->SetArrayField(TEXT("required"), RequiredArray);

    CommandsObj->SetObjectField(TEXT("items"), ItemsObj);
    PropertiesObj->SetObjectField(TEXT("Commands"), CommandsObj);
    SchemaObj->SetObjectField(TEXT("properties"), PropertiesObj);

    CachedCloudAISchema = SchemaObj;
}

TSharedPtr<FJsonObject> UDynamicCloudAISubsystem::GenerateCloudAIJsonSchema()
{
    if (!CachedCloudAISchema.IsValid())
    {
        BuildAndCacheJsonSchema();
    }
    return CachedCloudAISchema;
}

void UDynamicCloudAISubsystem::RequestCloudAIDirective(ADynamicCharacter* Character, const FString& ContextPrompt, FOnCloudAIDirectiveReceived OnCompleted)
{
    if (!Character)
    {
        OnCompleted.ExecuteIfBound(false, FCloudAIDirectiveResponse());
        return;
    }

    if (ActiveBackend == ECloudAIExecutionBackend::LocalRuleBasedFallback)
    {
        FCloudAIDirectiveResponse FallbackResponse;
        ExecuteRuleBasedFallback(Character, FallbackResponse);
        OnCompleted.ExecuteIfBound(true, FallbackResponse);
        return;
    }

    FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("POST"));
    Request->SetURL(EndpointUrl);
    Request->SetTimeout(10.0f);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!ApiKey.IsEmpty())
    {
        Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
    }

    TSharedRef<FJsonObject> RootPayload = MakeShareable(new FJsonObject());
    RootPayload->SetStringField(TEXT("model"), ModelName);

    TArray<TSharedPtr<FJsonValue>> Messages;
    TSharedRef<FJsonObject> UserMessage = MakeShareable(new FJsonObject());
    UserMessage->SetStringField(TEXT("role"), TEXT("user"));
    UserMessage->SetStringField(TEXT("content"), ContextPrompt);
    Messages.Add(MakeShareable(new FJsonValueObject(UserMessage)));
    RootPayload->SetArrayField(TEXT("messages"), Messages);

    TSharedPtr<FJsonObject> ResponseFormat = MakeShareable(new FJsonObject());
    ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
    ResponseFormat->SetObjectField(TEXT("schema"), GenerateCloudAIJsonSchema());
    RootPayload->SetObjectField(TEXT("response_format"), ResponseFormat);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootPayload, Writer);
    Request->SetContentAsString(OutputString);

    TWeakObjectPtr<ADynamicCharacter> WeakChar(Character);
    Request->OnProcessRequestComplete().BindLambda([this, WeakChar, OnCompleted](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
    {
        if (!bConnected || !Res.IsValid() || Res->GetResponseCode() < 200 || Res->GetResponseCode() >= 300)
        {
            if (WeakChar.IsValid())
            {
                FCloudAIDirectiveResponse FallbackResponse;
                ExecuteRuleBasedFallback(WeakChar.Get(), FallbackResponse);
                OnCompleted.ExecuteIfBound(true, FallbackResponse);
                return;
            }
            OnCompleted.ExecuteIfBound(false, FCloudAIDirectiveResponse());
            return;
        }

        const FString CleanJson = SanitizeMarkdownJson(Res->GetContentAsString());
        FCloudAIDirectiveResponse Directives;
        if (FJsonObjectConverter::JsonObjectStringToUStruct(CleanJson, &Directives, 0, 0))
        {
            OnCompleted.ExecuteIfBound(true, Directives);
        }
        else
        {
            if (WeakChar.IsValid())
            {
                FCloudAIDirectiveResponse FallbackResponse;
                ExecuteRuleBasedFallback(WeakChar.Get(), FallbackResponse);
                OnCompleted.ExecuteIfBound(true, FallbackResponse);
                return;
            }
            OnCompleted.ExecuteIfBound(false, FCloudAIDirectiveResponse());
        }
    });

    Request->ProcessRequest();
}

void UDynamicCloudAISubsystem::ExecuteRuleBasedFallback(ADynamicCharacter* Character, FCloudAIDirectiveResponse& OutFallbackResponse)
{
    OutFallbackResponse.Commands.Empty();
    if (!Character) return;

    FCloudAICommand DefaultCommand;

    if (const UDynamicSocialComponent* SocialComp = Character->FindComponentByClass<UDynamicSocialComponent>())
    {
        float HungerVal = 0.0f;
        static const FGameplayTag HungerTag = FGameplayTag::RequestGameplayTag(TEXT("Need.Hunger"), false);
        if (HungerTag.IsValid() && SocialComp->GetNeedValue(HungerTag, HungerVal) && HungerVal > 70.0f)
        {
            DefaultCommand.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Action.ConsumeFood"), false);
            DefaultCommand.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Tavern"), false);
            DefaultCommand.PriorityWeight = 90.0f;
            OutFallbackResponse.Commands.Add(DefaultCommand);
            return;
        }
    }

    DefaultCommand.CommandActionTag = FGameplayTag::RequestGameplayTag(TEXT("Schedule.Wander"), false);
    DefaultCommand.TargetEntityOrPlaceRoleTag = FGameplayTag::RequestGameplayTag(TEXT("Role.Place.Workplace"), false);
    DefaultCommand.PriorityWeight = 10.0f;
    OutFallbackResponse.Commands.Add(DefaultCommand);
}

FGuid UDynamicCloudAISubsystem::DispatchRemoteAIRequestNative(
    APawn* AgentPawn,
    ECloudAIRequestType RequestType,
    const FString& ContextJsonPrompt,
    float TimeoutSeconds,
    FOnCloudAIResponseDirectCallback DirectCallback)
{
    const FGuid RequestGuid = FGuid::NewGuid();
    ADynamicCharacter* DynChar = Cast<ADynamicCharacter>(AgentPawn);

    if (ActiveBackend == ECloudAIExecutionBackend::LocalRuleBasedFallback || !DynChar)
    {
        FCloudAIDirectiveResponse FallbackResponse;
        ExecuteRuleBasedFallback(DynChar, FallbackResponse);

        TArray<FCloudAICommandOutput> OutputCommands;
        for (const FCloudAICommand& Cmd : FallbackResponse.Commands)
        {
            FCloudAICommandOutput OutCmd;
            OutCmd.CommandActionTag = Cmd.CommandActionTag;
            OutCmd.TargetEntityOrPlaceRoleTag = Cmd.TargetEntityOrPlaceRoleTag;
            OutCmd.UtteranceText = Cmd.UtteranceText;
            OutCmd.PriorityWeight = Cmd.PriorityWeight;
            OutputCommands.Add(OutCmd);
        }

        DirectCallback.ExecuteIfBound(OutputCommands);
        return RequestGuid;
    }

    ActiveDirectCallbacks.Add(RequestGuid, DirectCallback);

    TWeakObjectPtr<UDynamicCloudAISubsystem> WeakThis(this);
    FOnCloudAIDirectiveReceived WrappedDelegate;
    WrappedDelegate.BindLambda([WeakThis, RequestGuid](bool bSuccess, const FCloudAIDirectiveResponse& Response)
    {
        if (!WeakThis.IsValid()) return;

        FOnCloudAIResponseDirectCallback FoundCallback;
        if (WeakThis->ActiveDirectCallbacks.RemoveAndCopyValue(RequestGuid, FoundCallback))
        {
            TArray<FCloudAICommandOutput> OutputCommands;
            for (const FCloudAICommand& Cmd : Response.Commands)
            {
                FCloudAICommandOutput OutCmd;
                OutCmd.CommandActionTag = Cmd.CommandActionTag;
                OutCmd.TargetEntityOrPlaceRoleTag = Cmd.TargetEntityOrPlaceRoleTag;
                OutCmd.UtteranceText = Cmd.UtteranceText;
                OutCmd.PriorityWeight = Cmd.PriorityWeight;
                OutputCommands.Add(OutCmd);
            }
            FoundCallback.ExecuteIfBound(OutputCommands);
        }
    });

    RequestCloudAIDirective(DynChar, ContextJsonPrompt, WrappedDelegate);
    return RequestGuid;
}

void UDynamicCloudAISubsystem::CancelRemoteAIRequest(const FGuid& RequestGuid)
{
    ActiveDirectCallbacks.Remove(RequestGuid);
}