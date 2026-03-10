// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ParsingSystemUEAddon : ModuleRules
{
	public ParsingSystemUEAddon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"NNE"
			}
			);
		
		string ThirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty"));
		string TokenizerPath = Path.Combine(ThirdPartyPath, "TokenizersCPP");
		
		PublicIncludePaths.Add(Path.Combine(TokenizerPath, "include"));
		
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(TokenizerPath, "lib/Win64", "tokenizers_cpp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(TokenizerPath, "lib/Win64", "tokenizers_c.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(TokenizerPath, "lib/Win64", "sentencepiece.lib"));
		}
		
		// REQUIRED FOR RUST
		PublicSystemLibraries.Add("Bcrypt.lib");
		PublicSystemLibraries.Add("Userenv.lib");
		PublicSystemLibraries.Add("ws2_32.lib");
		PublicSystemLibraries.Add("ntdll.lib");
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Projects"
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
