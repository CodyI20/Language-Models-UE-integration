// Fill out your copyright notice in the Description page of Project Settings.


#include "SpeechToTextComponent.h"

#include "AudioMixerBlueprintLibrary.h"
#include "EnhancedInputSubsystemInterface.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Core/Log.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"


USpeechToTextComponent::USpeechToTextComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USpeechToTextComponent::StartRecording()
{
	AudioCapture->Start();
	UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 0.f, SoundSubmix);
}

void USpeechToTextComponent::StopRecording()
{
	AudioCapture->Stop();
	FileToOverride = UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, RecordingName, WavFileDirectory,
		SoundSubmix, FileToOverride.Get());
}

void USpeechToTextComponent::SetWaVFileDirectory()
{
	FString SavedDir = FPaths::ProjectSavedDir();
	
	WavFileDirectory = FPaths::Combine(SavedDir, TEXT("STT_Recordings"));
	
	// Checking if the directory exists before writing to it
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*WavFileDirectory))
	{
		PlatformFile.CreateDirectory(*WavFileDirectory);
	}
}

void USpeechToTextComponent::SetFullAudioFilePath()
{
	if (WavFileDirectory.IsEmpty() || RecordingName.IsEmpty())
	{
		FString ErrorMessage = TEXT("Audio file path INACCESSIBLE! Either the wav file directory OR the recording name is invalid!");
		ULog::Error(TEXT("SpeechToTextComponent.cpp - SetFullAudioFilePath"), *ErrorMessage);
		return;
	}
	AudioFilePath =  FPaths::Combine(WavFileDirectory,FString::Printf(TEXT("%s.wav"), *RecordingName));
}

// Called when the game starts
void USpeechToTextComponent::BeginPlay()
{
	Super::BeginPlay();
	
	SetWaVFileDirectory();
	SetFullAudioFilePath();

	AActor* OwningActor = GetOwner();
	if (!OwningActor)
	{
		ULog::Error(TEXT("SpeechToTextComponent.cpp - BeginPlay"), TEXT("Actor is NULL"));
		return;
	}
	
	// Adds the audio capture to the local player character
	AudioCapture = static_cast<UAudioCaptureComponent*>(OwningActor->AddComponentByClass(UAudioCaptureComponent::StaticClass(),
		false,
		FTransform::Identity,
		true // bDeferredFinish being true allows for the injection of property changes before the component enters the world and starts its logic
		));
	
	if (!AudioCapture)
	{
		ULog::Error(TEXT("SpeechToTextComponent.cpp - BeginPlay"), TEXT("Audio Capture Component is NULL"));
		return;
	}
	
	// Prevents the component from automatically capturing and playing audio on game launch
	AudioCapture->bAutoActivate = false;
	AudioCapture->SoundSubmix = SoundSubmix;
	OwningActor->FinishAddComponent(AudioCapture, false, FTransform::Identity);
	
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	
	if (PlayerController && PlayerController->IsLocalController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			UInputMappingContext* MappingContext = LoadObject<UInputMappingContext>(nullptr,
				TEXT("/SpeechToText/Input/IMC_SpeechToText.IMC_SpeechToText"));
			if (!MappingContext)
			{
				ULog::Error(TEXT("SpeechToTextComponent.cpp - BeginPlay"), TEXT("The mapping context doesn't exist at the default path or it has been moved!"));
				return;
			}
			FModifyContextOptions Options;
			Options.bIgnoreAllPressedKeysUntilRelease = true;
			Options.bForceImmediately = false;
			Options.bNotifyUserSettings = false;
			
			InputSubsystem -> AddMappingContext(MappingContext,1,Options);
		}
	}
	
}

