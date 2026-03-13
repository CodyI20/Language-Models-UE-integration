// Fill out your copyright notice in the Description page of Project Settings.


#include "SpeechToTextComponent.h"

#include "EnhancedInputSubsystemInterface.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"


USpeechToTextComponent::USpeechToTextComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void USpeechToTextComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* OwningActor = GetOwner();
	if (!OwningActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Actor is NULL"));
		return;
	}
	
	AudioCapture = static_cast<UAudioCaptureComponent*>(OwningActor->AddComponentByClass(UAudioCaptureComponent::StaticClass(),
		false,
		FTransform::Identity,
		false
		));
	
	if (!AudioCapture)
	{
		UE_LOG(LogTemp, Error, TEXT("Audio Capture Component is NULL"));
		return;
	}

	AudioCapture->Activate(true);
	
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	
	if (PlayerController && PlayerController->IsLocalController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			UInputMappingContext* MappingContext = LoadObject<UInputMappingContext>(nullptr,
				TEXT("/SpeechToText/Input/IMC_SpeechToText.IMC_SpeechToText"));
			FModifyContextOptions Options;
			Options.bIgnoreAllPressedKeysUntilRelease = true;
			Options.bForceImmediately = false;
			Options.bNotifyUserSettings = false;
			
			InputSubsystem -> AddMappingContext(MappingContext,1,Options);
		}
	}
	
}

