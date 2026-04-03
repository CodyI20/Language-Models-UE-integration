// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextToSpeechComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "LuxTTSTokenizer.h"
#include "LuxTTSInference.h"
#include "Async/Async.h"
#include "UETTS.h" // Including the module header for the Custom Log

UTextToSpeechComponent::UTextToSpeechComponent()
{
	// Set this component to be initialized when the game starts
	PrimaryComponentTick.bCanEverTick = false;

	// Create the internal Audio Component and attach it to whoever owns this TTS component
	VoiceAudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("VoiceAudioComponent"));
	VoiceAudioComponent->bAutoActivate = false;
	VoiceAudioComponent->bAllowSpatialization = false;
	
	// If we are attaching to an Actor, handle the hierarchy
	if (GetOwner())
	{
		VoiceAudioComponent->SetupAttachment(GetOwner()->GetRootComponent());
	}
	if (VoiceAudioComponent == nullptr)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("The voice component is invalid!"))
	}
}

void UTextToSpeechComponent::BeginPlay()
{
	Super::BeginPlay();

	// Initialize the procedural sound wave
	ProceduralSoundWave = NewObject<USoundWaveProcedural>(this);
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
	if (TextToSpeak.IsEmpty() || !InferenceEngine.IsValid())
	{
		UE_LOG(LogTextToSpeech, Log, TEXT("Speak function called but the text is empty or the engine is invalid!"))
		return;
	}

	UE_LOG(LogTextToSpeech, Log, TEXT("AI is thinking on background thread..."));

	// AI Math off the game thread
	Async(EAsyncExecution::Thread, [this, TextToSpeak]()
	{
		TArray<int32> Tokens = Tokenizer->TokenizeText(TextToSpeak);
		TArray<float> TextFeats, AcousticFeats, AudioSamplesFloat;
		
		// DIAGNOSTIC LOG
		FString TokenString;
		for (int32 T : Tokens) TokenString += FString::Printf(TEXT("%d "), T);
		UE_LOG(LogTextToSpeech, Warning, TEXT("AI Received Tokens: %s"), *TokenString);

		// Run the Triple Pipeline
		if (InferenceEngine->RunTextEncoder(Tokens, TTSConfig.SpeechRate, TextFeats))
		{
			if (InferenceEngine->RunFMDecoder(TextFeats, AcousticFeats))
			{
				int32 AudioSeqLen = AcousticFeats.Num() / 100;
				
				if (InferenceEngine->RunVocoder(AcousticFeats, AudioSeqLen, AudioSamplesFloat))
				{
					// --- THE LIE DETECTOR ---
					int32 FMNaNs = 0, FMZeros = 0;
					for (float f : AcousticFeats) { 
						if (FMath::IsNaN(f)) FMNaNs++; 
						else if (FMath::Abs(f) < 0.000001f) FMZeros++;
					}

					int32 VocoderNaNs = 0, VocoderZeros = 0;
					for (float f : AudioSamplesFloat) { 
						if (FMath::IsNaN(f)) VocoderNaNs++; 
						else if (FMath::Abs(f) < 0.000001f) VocoderZeros++;
					}

					UE_LOG(LogTextToSpeech, Error, TEXT("DIAGNOSTICS -> FM NaNs: %d | FM Zeros: %d | Vocoder NaNs: %d | Vocoder Zeros: %d"), FMNaNs, FMZeros, VocoderNaNs, VocoderZeros);
					// ------------------------

					float MaxAmplitude = 0.0f;
					for (float Sample : AudioSamplesFloat)
					{
						if (FMath::Abs(Sample) > MaxAmplitude) MaxAmplitude = FMath::Abs(Sample);
					}
					
					// This log will tell us if the AI math exploded or stabilized!
					UE_LOG(LogTextToSpeech, Warning, TEXT("AI Peak Amplitude: %f"), MaxAmplitude);

					TArray<int16> PcmAudio;
					PcmAudio.SetNumUninitialized(AudioSamplesFloat.Num());
					
					for (int32 i = 0; i < AudioSamplesFloat.Num(); ++i)
					{
						// Safely convert to PCM. If it's too loud, the Clamp will save your speakers.
						PcmAudio[i] = static_cast<int16>(FMath::Clamp(AudioSamplesFloat[i] * 32767.0f * TTSConfig.VolumeMultiplier, -32768.0f, 32767.0f));
					}

					AsyncTask(ENamedThreads::GameThread, [this, PcmAudio]()
					{
						if (!ProceduralSoundWave || !VoiceAudioComponent) return;

						if (!VoiceAudioComponent->IsPlaying()) ProceduralSoundWave->ResetAudio();

						ProceduralSoundWave->QueueAudio(reinterpret_cast<const uint8*>(PcmAudio.GetData()), PcmAudio.Num() * sizeof(int16));
						
						VoiceAudioComponent->Activate(true);
						if (!VoiceAudioComponent->IsPlaying()) VoiceAudioComponent->Play();
					});
				}
			}
		}
	});
}