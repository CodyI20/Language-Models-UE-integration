// Copyright Epic Games, Inc. All Rights Reserved.

#include "UETTS.h"

#define LOCTEXT_NAMESPACE "FUETTSModule"

DEFINE_LOG_CATEGORY(LogTextToSpeech);

void FUETTSModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	UE_LOG(LogTextToSpeech, Log, TEXT("UETTS Module has started successfully!"));
}

void FUETTSModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	
	UE_LOG(LogTextToSpeech, Log, TEXT("UETTS Module is shutting down."));
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FUETTSModule, UETTS)