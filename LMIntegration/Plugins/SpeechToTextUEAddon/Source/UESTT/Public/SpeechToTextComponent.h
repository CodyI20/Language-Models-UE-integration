// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AudioCaptureComponent.h"
#include "SpeechToTextComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UESTT_API USpeechToTextComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	USpeechToTextComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	
	UAudioCaptureComponent* AudioCapture;
};
