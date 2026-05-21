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
				"JsonUtilities",
				"DeveloperSettings" 
			}
		);
		
		string thirdPartyPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty"));
		string tokenizerPath = Path.Combine(thirdPartyPath, "TokenizersCPP");
		
		PublicIncludePaths.Add(Path.Combine(tokenizerPath, "include"));
		
		if (target.Platform == UnrealTargetPlatform.Win64)
		{
			// Third-party static libraries for Windows
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Win64", "tokenizers_cpp.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Win64", "tokenizers_c.lib"));
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Win64", "sentencepiece.lib"));
			
			// REQUIRED FOR RUST ON WINDOWS
			// These are Windows API system libraries used by Rust's standard library
			PublicSystemLibraries.Add("Bcrypt.lib");
			PublicSystemLibraries.Add("Userenv.lib");
			PublicSystemLibraries.Add("ws2_32.lib");
			PublicSystemLibraries.Add("ntdll.lib");
		}
		else if (target.Platform == UnrealTargetPlatform.Android)
		{
			// Third-party static libraries for Android

			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Android", "libtokenizers_cpp.a"));
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Android", "libtokenizers_c.a"));
			PublicAdditionalLibraries.Add(Path.Combine(tokenizerPath, "lib/Android", "libsentencepiece.a"));
			
		}

		// Ensures the Content folder is available in the packaged build across all platforms
		RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Content", "*"), StagedFileType.NonUFS);
		

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
				"Projects", 
				"UTLogger"
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