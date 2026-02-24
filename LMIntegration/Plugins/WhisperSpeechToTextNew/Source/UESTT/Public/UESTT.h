// Copyright 2025 Lukas7251. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

// Forward declare the whisper context so we don't need to include whisper.h here
struct whisper_context;

class FSpeechToTextModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	bool IsGPUAccelerationAvailable() const;
	FString GetActiveBinariesPath() const { return ActiveBinariesPath; }

	// NEW: Gets the cached whisper context, or loads it if it hasn't been loaded yet.
	struct whisper_context* GetOrLoadGlobalContext(const FString& ModelPath, bool bUseGPU);

private:
	bool TryLoadBinariesFromPath(const FString& BinariesPath);
	void LoadAppropriateLibraries();

	TArray<void*> LoadedDllHandles;
	bool bGPUAccelerationAvailable;
	bool bInitialized;
	FString ActiveBinariesPath;

	// NEW: Caching Variables
	struct whisper_context* CachedWhisperContext = nullptr;
	FString CachedModelPath;
	bool bCachedWithGPU = false;
	
	// NEW: A lock to ensure thread safety if multiple threads try to load the model at once
	FCriticalSection ContextLock; 
};