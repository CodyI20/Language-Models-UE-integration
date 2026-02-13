// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "whisper.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * 
 */
class WHISPERSPEECHTOTEXT_API FWhisperSpeechToText : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	static inline FWhisperSpeechToText& Get() {
		return FModuleManager::GetModuleChecked<FWhisperSpeechToText>("WhisperSpeechToText");
	}
	
	bool InitializeModel(const FString& ModelPath);
	FString TranscribeFromBuffer(const float* PCMData, int32 SampleCount);
	
private:
	whisper_context* _context = nullptr;
};
