// Copyright Epic Games, Inc. All Rights Reserved.
// ReSharper disable All

using System.IO;
using UnrealBuildTool;

[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE1006:Naming Styles", Justification = "Unreal module rules class name follows the module name convention")]
public class ParsingSystemUEAddon : ModuleRules
{
	public ParsingSystemUEAddon(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"NNE",
				"Json",
				"JsonUtilities"
			}
			);
		
		string thirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty"));
		string tokenizerPath = Path.Combine(thirdPartyPath, "TokenizersCPP");
		
		PublicIncludePaths.Add(Path.Combine(tokenizerPath, "include"));
		
		if (target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Win64", "tokenizers_cpp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Win64", "tokenizers_c.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Win64", "sentencepiece.lib"));
			
			// Ensures the Content folder is available in the packaged build
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Content", "*"), StagedFileType.NonUFS);
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
			new[]
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

// ReSharper restore All

