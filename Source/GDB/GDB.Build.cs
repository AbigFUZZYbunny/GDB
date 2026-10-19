// Fill out your copyright notice in the Description page of Project Settings.
using System.IO;
using UnrealBuildTool;

public class GDB : ModuleRules
{
	public GDB(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"GameplayTags", 
			"GameplayAbilities", 
			"GameplayTasks",
			"Json", 
			"JsonUtilities",
			"NetCore",
			"Mover",               // Required for Mover Component
			"PhysicsControl",       // Required for Active Ragdoll 5.8 configurations 
			"MotionTrajectory",
			"AIModule"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
		
		PublicIncludePaths.AddRange(
			new string[] {
				// If you want to include files directly from a custom folder inside "Public"
				Path.Combine(ModuleDirectory, "GAS")
			}
		);
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// If you want to include files directly from a custom folder inside "Public"
				Path.Combine(ModuleDirectory, "GAS")
			}
		);
	}
}
