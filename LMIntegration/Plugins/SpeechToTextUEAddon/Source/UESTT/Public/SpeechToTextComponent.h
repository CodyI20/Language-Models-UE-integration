// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureComponent.h"
#include "SpeechToTextComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnFinishedSTT, bool, SuccessResult, FString, TextResult, float, ProcessingTimeResult, FString, ErrorMessageResult);

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
	
	UFUNCTION(BlueprintCallable, Category = "SpeechToText")
	void StartRecording();
	
	UFUNCTION(BlueprintCallable, Category = "SpeechToText")
	void StopRecording();
	
private:
	void SetWaVFileDirectory();
	void SetFullAudioFilePath();
	FString WavFileDirectory;
	
	TWeakObjectPtr<USoundWave> FileToOverride;
};
