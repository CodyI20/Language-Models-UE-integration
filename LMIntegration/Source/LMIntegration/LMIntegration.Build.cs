// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class LMIntegration : ModuleRules
{
	public LMIntegration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "NNE" });
		
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty"));
		string TokenizerPath = Path.Combine(ThirdPartyPath, "TokenizersCPP");
		
		PublicIncludePaths.Add(Path.Combine(TokenizerPath, "include"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(TokenizerPath, "lib", "tokenizers_cpp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(TokenizerPath, "lib", "tokenizers_c.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(TokenizerPath, "lib", "sentencepiece.lib"));
		}
		
		// REQUIRED FOR RUST
		PublicSystemLibraries.Add("Bcrypt.lib");
		PublicSystemLibraries.Add("Userenv.lib");
		PublicSystemLibraries.Add("ws2_32.lib");
		PublicSystemLibraries.Add("ntdll.lib");

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
