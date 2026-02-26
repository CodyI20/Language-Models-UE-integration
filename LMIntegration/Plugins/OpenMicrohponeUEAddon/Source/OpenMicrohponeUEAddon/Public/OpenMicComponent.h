// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "OpenMicComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam (FBroadcastAudioFromMicrophone, const TArray<float>&, AudioSamples);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class OPENMICROHPONEUEADDON_API UOpenMicComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UOpenMicComponent();
	
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
