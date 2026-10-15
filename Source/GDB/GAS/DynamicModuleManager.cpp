// Fill out your copyright notice in the Description page of Project Settings.


#include "DynamicModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "DynamicDataSerializer.h"
#include "DynamicGameState.h"

TArray<FString> UDynamicModuleManager::GetAvailableModuleProfiles()
{
	TArray<FString> FoundProfiles;
	IFileManager& FileManager = IFileManager::Get();
	FString SearchPath = FPaths::ProjectSavedDir() / TEXT("DynamicModules/*.json");
    
	TArray<FString> Files;
	FileManager.FindFiles(Files, *SearchPath, true, false);

	for (const FString& File : Files)
	{
		FoundProfiles.Add(FPaths::GetBaseFilename(File));
	}
	return FoundProfiles;
}

bool UDynamicModuleManager::SaveCurrentRuntimeToProfile(const ADynamicGameState* InGameState, FString ProfileName)
{
	if (!InGameState || ProfileName.IsEmpty()) return false;

	FString SerializedJson;
	if (UDynamicDataSerializer::SerializeMasterRegistry(InGameState, SerializedJson))
	{
		FString SavePath = FPaths::ProjectSavedDir() / TEXT("DynamicModules") / (ProfileName + TEXT(".json"));
		return FFileHelper::SaveStringToFile(SerializedJson, *SavePath);
	}
	return false;
}

bool UDynamicModuleManager::InitializeSessionWithModules(ADynamicGameState* TargetGameState, const TArray<FString>& ActiveModuleNames)
{
	if (!TargetGameState) return false;

	for (const FString& ModuleName : ActiveModuleNames)
	{
		FString LoadPath = FPaths::ProjectSavedDir() / TEXT("DynamicModules") / (ModuleName + TEXT(".json"));
		FString JsonData;

		if (FFileHelper::LoadFileToString(JsonData, *LoadPath))
		{
			// Merges structures sequentially into our unified, replicated game-state registries
			UDynamicDataSerializer::DeserializeMasterRegistry(JsonData, TargetGameState);
		}
	}
	return true;
}
