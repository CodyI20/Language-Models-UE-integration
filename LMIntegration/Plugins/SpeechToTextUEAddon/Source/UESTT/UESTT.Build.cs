// Copyright 2025 Lukas7251. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using System;

public class UESTT : ModuleRules
{
	public UESTT(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Warning;
		PrecompileForTargets = PrecompileTargetsType.Any;
		bLegacyPublicIncludePaths = false;
		IWYUSupport = IWYUSupport.None;
        

		// Use Source/ThirdParty layout only
		string TP_Source = Path.Combine(ModuleDirectory, "../ThirdParty/whisper");
		
		// Include path
		PublicIncludePaths.Add(Path.Combine(TP_Source, "include"));
        
		// WINDOWS
		if(Target.Platform == UnrealTargetPlatform.Win64)
		{
			string[] LibCandidates = new string[]
			{
				Path.Combine(TP_Source, "lib/Win64/Release/whisper.lib"),
			};

			string SelectedLib = null;
			foreach (var candidate in LibCandidates)
			{
				if (File.Exists(candidate))
				{
					SelectedLib = candidate;
					break;
				}
			}

			if (SelectedLib == null)
			{
				throw new BuildException($"Whisper import library not found. Checked: {string.Join(", ", LibCandidates)}");
			}
			PublicAdditionalLibraries.Add(SelectedLib);

			PublicDelayLoadDLLs.Add("whisper.dll");
			PublicDelayLoadDLLs.Add("ggml.dll");
			PublicDelayLoadDLLs.Add("ggml-base.dll");
			PublicDelayLoadDLLs.Add("ggml-cpu.dll");
			PublicDelayLoadDLLs.Add("ggml-cuda.dll");
			PublicDelayLoadDLLs.Add("cublas64_12.dll");
			PublicDelayLoadDLLs.Add("cublasLt64_12.dll");
			PublicDelayLoadDLLs.Add("cudart64_12.dll");
			
			// GGML & Whisper dependencies for the GPU
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/whisper.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/ggml.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/ggml-base.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/ggml-cpu.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/ggml-cuda.dll", StagedFileType.NonUFS);

			// Explicitly stage the CUDA Runtime and cuBLAS libraries
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/cudart64_12.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/cublas64_12.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_GPU/cublasLt64_12.dll", StagedFileType.NonUFS);

			// CPU Fallback
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_CPU/whisper.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_CPU/ggml.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_CPU/ggml-base.dll", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(PluginDir)/Source/ThirdParty/whisper/bin/Win64_CPU/ggml-cpu.dll", StagedFileType.NonUFS);
		}
		// ANDROID
		else if (Target.Platform == UnrealTargetPlatform.Android)
		{
			// Point this to the folder containing your compiled .a files
			string AndroidLibDir = Path.Combine(TP_Source, "lib/Android/Release"); 

			// Provide the libraries to the linker to fix the "undefined symbol" errors
			PublicAdditionalLibraries.Add(Path.Combine(AndroidLibDir, "libwhisper.a"));
			PublicAdditionalLibraries.Add(Path.Combine(AndroidLibDir, "libggml.a"));
			PublicAdditionalLibraries.Add(Path.Combine(AndroidLibDir, "libggml-base.a"));
			PublicAdditionalLibraries.Add(Path.Combine(AndroidLibDir, "libggml-cpu.a"));
			
			// Note: We do not need PublicDelayLoadDLLs or RuntimeDependencies for Android 
			// because the static (.a) libraries are baked directly into the game's executable!
		}
        
		// Ensures the Content folder is available in the packaged build
		RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Content", "*"), StagedFileType.NonUFS);

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "Projects", "AudioCapture", "AudioMixer", "RuntimeAudioImporter"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EnhancedInput", "UTLogger"
		});
	}
}