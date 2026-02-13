// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AudioMixerDevice.h"
#include "AudioDeviceManager.h"
#include "Engine/World.h"
#include "WhisperBPLibrary.generated.h"

/**
 * 
 */

// Only one recording data should be available at once
static TUniquePtr<Audio::FAudioRecordingData> RecordData;

UCLASS()
class WHISPERSPEECHTOTEXT_API UWhisperBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Whisper Integration", meta = (WorldContext = "WorldContextObject", DisplayName = "Translate"))
	static FString TranslateMicToText(const UObject* WorldContextObject, USoundSubmix* SubmixToRecord = nullptr);
	
	UFUNCTION(BlueprintCallable, Category = "Whisper Integration", meta = (DisplayName = "Transcribe"))
	static FString TranscribeAudioBuffer(const TArray<float> &PCMData);
};
