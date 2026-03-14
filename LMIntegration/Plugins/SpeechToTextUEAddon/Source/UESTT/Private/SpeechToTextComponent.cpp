// Fill out your copyright notice in the Description page of Project Settings.


#include "SpeechToTextComponent.h"

#include "AudioMixerBlueprintLibrary.h"
#include "EnhancedInputSubsystemInterface.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Interfaces/IPluginManager.h"


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
		UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
		// On-screen
		UKismetSystemLibrary::PrintString(this, *ErrorMessage,
	true, false, FLinearColor::Red, 100.f, NAME_Error);
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
		UE_LOG(LogTemp, Warning, TEXT("Actor is NULL"));
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
		UE_LOG(LogTemp, Error, TEXT("Audio Capture Component is NULL"));
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
				UE_LOG(LogTemp, Error, TEXT("The mapping context doesn't exist at the default path or it has been moved!"));
				// On-screen
				UKismetSystemLibrary::PrintString(this, TEXT("The mapping context doesn't exist at the default path or it has been moved!"),
			true, false, FLinearColor::Red, 100.f, NAME_Error);
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

