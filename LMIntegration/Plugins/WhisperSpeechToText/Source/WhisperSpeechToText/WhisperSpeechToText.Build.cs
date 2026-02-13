// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class WhisperSpeechToText : ModuleRules
{
	public WhisperSpeechToText(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		string ThirdPartyPath = Path.Combine(ModuleDirectory, "..", "ThirdParty", "whisper");
		PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "include"));

		string LibPath = Path.Combine(ThirdPartyPath, "lib", "Win64", "Release");
		PublicAdditionalLibraries.Add(Path.Combine(LibPath, "whisper.lib"));

		string DllPathCPU = Path.Combine(ThirdPartyPath, "bin", "Win64_CPU");
		string[] CPUDlls = Directory.GetFiles(DllPathCPU, "*.dll");
		
		string DllPathGPU = Path.Combine(ThirdPartyPath, "bin", "Win64_GPU");
		string[] GPUDlls = Directory.GetFiles(DllPathGPU, "*.dll");
		
		PublicRuntimeLibraryPaths.Add(Path.Combine(ThirdPartyPath, "bin", "Win64_CPU"));
		PublicRuntimeLibraryPaths.Add(Path.Combine(ThirdPartyPath, "bin", "Win64_GPU"));

		foreach (string dll in CPUDlls)
		{
			RuntimeDependencies.Add(dll);
		}

		foreach (string dll in GPUDlls)
		{
			RuntimeDependencies.Add(dll);
		}
		
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
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"AudioCaptureCore",
				"SignalProcessing"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
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
