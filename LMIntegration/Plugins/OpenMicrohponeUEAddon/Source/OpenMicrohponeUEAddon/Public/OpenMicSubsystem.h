// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OpenMicSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam (FBroadcastAudioFromMicrophone, const TArray<float>&, AudioSamples);

UCLASS( ClassGroup=(Custom))
class OPENMICROHPONEUEADDON_API UOpenMicSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:	
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	
	UPROPERTY(BlueprintAssignable, Category = "Microphone")
	FBroadcastAudioFromMicrophone OnAudioCaptured;
	
	UFUNCTION(BlueprintCallable, Category = "Microphone")
	void StartMicrophone();
	
	UFUNCTION(BlueprintCallable, Category = "Microphone")
	void StopMicrophone();
	
private:
	void CaptureAudioTick();
	TSharedPtr<class IVoiceCapture> VoiceCapture;
	FTimerHandle AudioCaptureTimerHandle;
};
