// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureComponent.h"
#include "Sound/CapturableSoundWave.h"
#include "SpeechToTextComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnFinishedSTT, bool, SuccessResult, FString, TextResult, float, ProcessingTimeResult, FString, ErrorMessageResult);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRecordingStopped);

UCLASS(BLueprintable, ClassGroup = (Custom))
class UESTT_API USpeechToTextComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	USpeechToTextComponent();
	

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SpeechToText")
	FOnFinishedSTT OnFinishedSTT;
	
	UPROPERTY(EditAnywhere, Category = "SpeechToText")
	FString RecordingName;
	
	UPROPERTY(BlueprintReadOnly, Category = "SpeechToText")
	TObjectPtr<UAudioCaptureComponent> AudioCapture;
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "SpeechToText")
	TObjectPtr<USoundSubmix> SoundSubmix;
	
	UPROPERTY(BlueprintReadOnly, Category = "SpeechToText")
	FString AudioFilePath;
	
	UPROPERTY(BlueprintAssignable, Category = "SpeechToText")
	FOnRecordingStopped OnRecordingStopped;
	
	UPROPERTY()
	UCapturableSoundWave* CapturableSoundWave;
	
	UFUNCTION(BlueprintCallable, Category = "SpeechToText")
	void StartRecording();
	
	UFUNCTION(BlueprintCallable, Category = "SpeechToText")
	void StopRecording();
	
	// Called in BeginPlay to set up the VAD and subscribe to its delegates
	// Could be manually called in BP if needed
	UFUNCTION(BlueprintCallable, Category = "SpeechToText")
	void SetupVAD();
	
private:
	void SetWaVFileDirectory();
	void SetFullAudioFilePath();
	FString WavFileDirectory;
	
	TWeakObjectPtr<USoundWave> FileToOverride;
};
