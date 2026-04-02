// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextToSpeechComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "LuxTTSTokenizer.h"
#include "LuxTTSInference.h"
#include "UETTS.h" // Including the module header for the Custom Log

UTextToSpeechComponent::UTextToSpeechComponent()
{
	// Set this component to be initialized when the game starts
	PrimaryComponentTick.bCanEverTick = false;

	// Create the internal Audio Component and attach it to whoever owns this TTS component
	VoiceAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("VoiceAudioComponent"));
	VoiceAudioComponent->bAutoActivate = false;
	
	// If we are attaching to an Actor, handle the hierarchy
	if (GetOwner())
	{
		VoiceAudioComponent->SetupAttachment(GetOwner()->GetRootComponent());
	}
}

void UTextToSpeechComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize the procedural sound wave
	ProceduralSoundWave = NewObject<USoundWaveProcedural>();
	ProceduralSoundWave->SetSampleRate(24000); 
	ProceduralSoundWave->NumChannels = 1; // Mono audio
	ProceduralSoundWave->Duration = INDEFINITELY_LOOPING_DURATION; 
	ProceduralSoundWave->SoundGroup = SOUNDGROUP_Voice;
	ProceduralSoundWave->bLooping = false;

	// Assign the procedural wave to the audio component
	if (VoiceAudioComponent)
	{
		VoiceAudioComponent->SetSound(ProceduralSoundWave);
	}
	
	Tokenizer = MakeShared<FLuxTTSTokenizer>();
	Tokenizer->LoadVocabulary();
	
	InferenceEngine = MakeShared<FLuxTTSInference>();
	InferenceEngine->InitializeModels();

	UE_LOG(LogTextToSpeech, Log, TEXT("TextToSpeech Component Initialized on Actor: %s"), *GetOwner()->GetName());
}

void UTextToSpeechComponent::Speak(const FString& TextToSpeak)
{
	if (TextToSpeak.IsEmpty())
	{
		UE_LOG(LogTextToSpeech, Warning, TEXT("Speak function called, but the text was empty!"));
		return;
	}

	UE_LOG(LogTextToSpeech, Log, TEXT("Preparing to speak text: %s"), *TextToSpeak);

	TArray<int32> Tokens = Tokenizer->TokenizeText(TextToSpeak);
	TArray<float> TextFeatures;
	TArray<float> AcousticFeatures; 
	TArray<float> AudioSamples;

	if (InferenceEngine.IsValid())
	{
		if (InferenceEngine->RunTextEncoder(Tokens, TTSConfig.SpeechRate, TextFeatures))
		{
			if (InferenceEngine->RunFMDecoder(TextFeatures, AcousticFeatures))
			{
				int32 AudioSeqLen = AcousticFeatures.Num() / 100;
				InferenceEngine->RunVocoder(AcousticFeatures, AudioSeqLen, AudioSamples);
			}
		}
	}
}