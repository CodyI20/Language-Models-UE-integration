#include "SpeechToTextComponent.h"
#include "EnhancedInputSubsystemInterface.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Core/Log.h"
#include "HAL/PlatformFileManager.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Async/Async.h"
#include "RuntimeAudioExporter.h"
#include "Misc/Paths.h"

USpeechToTextComponent::USpeechToTextComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USpeechToTextComponent::StartRecording()
{
	ULog::Info(TEXT("SpeechToTextComponent"), TEXT("VAD triggered. Relying on existing CapturableSoundWave buffer..."));
}

void USpeechToTextComponent::StopRecording()
{
	if (!CapturableSoundWave) return;
	
	CapturableSoundWave->StopCapture();

	FString FullFilePath = FPaths::Combine(WavFileDirectory, RecordingName + TEXT(".wav"));
    
	ULog::Info(TEXT("SpeechToTextComponent"), FString::Printf(TEXT("Exporting audio to: %s"), *FullFilePath));
	
	URuntimeAudioExporter::ExportSoundWaveToFile(
		CapturableSoundWave,
		FullFilePath,
		ERuntimeAudioFormat::Wav,
		100, // Quality (100 is max, standard for lossless WAV)
		FRuntimeAudioExportOverrideOptions(), // Empty for default sample rate
		FOnAudioExportToFileResultNative::CreateWeakLambda(this, [this](bool bSucceeded)
		{
			if (bSucceeded)
			{
				ULog::Info(TEXT("SpeechToTextComponent"), TEXT("Successfully exported VAD audio to WAV!"));
                
				if (this->OnRecordingStopped.IsBound())
				{
				   this->OnRecordingStopped.Broadcast();
				}
			}
			else
			{
				ULog::Error(TEXT("SpeechToTextComponent"), TEXT("Failed to export WAV file."));
			}
			CapturableSoundWave->ReleaseMemory();
			CapturableSoundWave->StartCapture(INDEX_NONE);
			
		})
	);
}

void USpeechToTextComponent::SetupVAD()
{
	ULog::Info(TEXT("SpeechToTextComponent.cpp - SetupVAD"), TEXT("Setting up VAD..."));
	CapturableSoundWave = UCapturableSoundWave::CreateCapturableSoundWave();
	
	if (!CapturableSoundWave)
	{
		ULog::Error(TEXT("SpeechToTextComponent.cpp - SetupVAD"), TEXT("Failed to create CapturableSoundWave!"));
		return;
	}
	
	CapturableSoundWave->ToggleVAD(true);
	CapturableSoundWave->SetMinimumSpeechDuration(250.0f);
	CapturableSoundWave->SetSilenceDuration(200.0f);
	
	
	// Subscribe to speech detection delegates
	CapturableSoundWave->OnSpeechStartedNative.AddWeakLambda(this, [this]()
	{
	   AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechToTextComponent>(this)]()
	   {
		   if (WeakThis.IsValid())
		   {
			   ULog::Info(TEXT("SpeechToTextComponent.cpp"), TEXT("Speech started detected on Game Thread!"));
			   WeakThis->StartRecording();
		   }
	   });
	});
	
	CapturableSoundWave->OnSpeechEndedNative.AddWeakLambda(this, [this]()
	{
	   AsyncTask(ENamedThreads::GameThread, [WeakThis = TWeakObjectPtr<USpeechToTextComponent>(this)]()
	   {
		   if (WeakThis.IsValid())
		   {
			   ULog::Info(TEXT("SpeechToTextComponent.cpp"), TEXT("Speech stopped detected on Game Thread!"));
			   WeakThis->StopRecording();
		   }
	   });
	});
	
	CapturableSoundWave->StartCapture(INDEX_NONE);
	//
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
	SetupVAD();
	
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

