// Copyright 2025 Lukas7251. All Rights Reserved.

#include "UESTT.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogUESTT, Log, All);

#define LOCTEXT_NAMESPACE "FSpeechToTextModule"

void FSpeechToTextModule::StartupModule()
{
	bGPUAccelerationAvailable = false;
	bInitialized = false;
	LoadedDllHandles.Reset();
	
	LoadAppropriateLibraries();
	
	if (!bInitialized)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WhisperDllError", 
			"Failed to load whisper DLLs. The speech-to-text functionality will not be available. "
			"Please make sure you have installed the Visual C++ Redistributable for Visual Studio 2019 or newer."));
	}
}

void FSpeechToTextModule::LoadAppropriateLibraries()
{
	FString BaseDir = IPluginManager::Get().FindPlugin("SpeechToText")->GetBaseDir();
    
	// Use Source/ThirdParty layout
	const FString SRC_TP_GpuPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/whisper/bin/Win64_GPU"));
	const FString SRC_TP_CpuPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/whisper/bin/Win64_CPU"));

	// Try Source/ThirdParty GPU, then CPU
	if (TryLoadBinariesFromPath(SRC_TP_GpuPath))
	{
		UE_LOG(LogUESTT, Log, TEXT("Initialized with CUDA GPU-accelerated binaries (Source/ThirdParty)"));
		bGPUAccelerationAvailable = true;
		bInitialized = true;
		ActiveBinariesPath = SRC_TP_GpuPath;
		return;
	}

	if (TryLoadBinariesFromPath(SRC_TP_CpuPath))
	{
		UE_LOG(LogUESTT, Log, TEXT("Initialized with CPU-only binaries (Source/ThirdParty)"));
		bGPUAccelerationAvailable = false;
		bInitialized = true;
		ActiveBinariesPath = SRC_TP_CpuPath;
		return;
	}

	UE_LOG(LogUESTT, Warning, TEXT("Failed to load whisper/ggml binaries from any known directory."));
}

bool FSpeechToTextModule::TryLoadBinariesFromPath(const FString& BinariesPath)
{
	for (void* Handle : LoadedDllHandles)
	{
		if (Handle)
		{
			FPlatformProcess::FreeDllHandle(Handle);
		}
	}
	LoadedDllHandles.Reset();
	
	if (!FPaths::DirectoryExists(BinariesPath))
	{
		UE_LOG(LogUESTT, Warning, TEXT("Binaries directory does not exist: %s"), *BinariesPath);
		return false;
	}
	
	FPlatformProcess::AddDllDirectory(*BinariesPath);
	
	FString GgmlDllPath = FPaths::Combine(*BinariesPath, TEXT("ggml.dll"));
	void* GgmlDllHandle = FPlatformProcess::GetDllHandle(*GgmlDllPath);
	
	FString GgmlBaseDllPath = FPaths::Combine(*BinariesPath, TEXT("ggml-base.dll"));
	void* GgmlBaseDllHandle = FPlatformProcess::GetDllHandle(*GgmlBaseDllPath);
	
	FString GgmlCpuDllPath = FPaths::Combine(*BinariesPath, TEXT("ggml-cpu.dll"));
	void* GgmlCpuDllHandle = FPlatformProcess::GetDllHandle(*GgmlCpuDllPath);
	
	FString GgmlCudaDllPath = FPaths::Combine(*BinariesPath, TEXT("ggml-cuda.dll"));
	void* GgmlCudaDllHandle = FPlatformProcess::GetDllHandle(*GgmlCudaDllPath);
	
	if (GgmlCudaDllHandle) 
	{
		UE_LOG(LogUESTT, Log, TEXT("CUDA GPU acceleration is available from path: %s"), *BinariesPath);
	} 
	else 
	{
		UE_LOG(LogUESTT, Log, TEXT("CUDA GPU acceleration is NOT available from path: %s"), *BinariesPath);
	}
	
	FString WhisperDllPath = FPaths::Combine(*BinariesPath, TEXT("whisper.dll"));
	void* WhisperDllHandle = FPlatformProcess::GetDllHandle(*WhisperDllPath);
	
	if (GgmlDllHandle) LoadedDllHandles.Add(GgmlDllHandle);
	if (GgmlBaseDllHandle) LoadedDllHandles.Add(GgmlBaseDllHandle);
	if (GgmlCpuDllHandle) LoadedDllHandles.Add(GgmlCpuDllHandle);
	if (GgmlCudaDllHandle) LoadedDllHandles.Add(GgmlCudaDllHandle);
	if (WhisperDllHandle) LoadedDllHandles.Add(WhisperDllHandle);
	
	bool bSuccess = (WhisperDllHandle != nullptr && GgmlDllHandle != nullptr && 
					GgmlBaseDllHandle != nullptr && GgmlCpuDllHandle != nullptr);
	
	if (!bSuccess)
	{
	    UE_LOG(LogUESTT, Warning, TEXT("Failed to load required DLLs from path: %s"), *BinariesPath);
	    UE_LOG(LogUESTT, Warning, TEXT("If this is a fresh machine, install 'Microsoft Visual C++ Redistributable for Visual Studio 2015-2022 (x64)'."));
	}
	
	return bSuccess;
}

void FSpeechToTextModule::ShutdownModule()
{
	for (void* Handle : LoadedDllHandles)
	{
		if (Handle)
		{
			FPlatformProcess::FreeDllHandle(Handle);
		}
	}
	LoadedDllHandles.Empty();
}

bool FSpeechToTextModule::IsGPUAccelerationAvailable() const
{
	return bGPUAccelerationAvailable;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSpeechToTextModule, UESTT)
