#pragma once

#include "Modules/ModuleManager.h"

class FSpeechToTextModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	bool IsGPUAccelerationAvailable() const;
	// Returns the binaries directory path from which DLLs were loaded (Source/ThirdParty/whisper/bin/Win64_GPU or Win64_CPU).
	FString GetActiveBinariesPath() const { return ActiveBinariesPath; }

private:
	bool TryLoadBinariesFromPath(const FString& BinariesPath);
	void LoadAppropriateLibraries();

	TArray<void*> LoadedDllHandles;
	bool bGPUAccelerationAvailable;
	bool bInitialized;
	FString ActiveBinariesPath;
};
