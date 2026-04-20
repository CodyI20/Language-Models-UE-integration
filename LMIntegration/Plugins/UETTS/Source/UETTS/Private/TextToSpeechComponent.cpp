// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextToSpeechComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "LuxTTSTokenizer.h"
#include "LuxTTSInference.h"
#include "Async/Async.h"
#include "Audio.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UETTS.h" // Including the module header for the Custom Log

namespace
{
	constexpr int32 TargetPromptSampleRate = 24000;
	constexpr int32 PromptFeatureDim = 100;

	static float HzToMel(const float Hz)
	{
		return 2595.0f * FMath::LogX(10.0f, 1.0f + (Hz / 700.0f));
	}

	static float MelToHz(const float Mel)
	{
		return 700.0f * (FMath::Pow(10.0f, Mel / 2595.0f) - 1.0f);
	}

	static bool LoadMono24kFromWav(const FString& WavPath, float MaxSeconds, TArray<float>& OutMono24k)
	{
		OutMono24k.Reset();
		if (WavPath.IsEmpty())
		{
			UE_LOG(LogTextToSpeech, Error, TEXT("Reference WAV path is empty"));
			return false;
		}

		FString ResolvedPath = WavPath;
		if (!IFileManager::Get().FileExists(*ResolvedPath))
		{
			ResolvedPath = FPaths::ProjectContentDir() + WavPath;
			if (!IFileManager::Get().FileExists(*ResolvedPath))
			{
				ResolvedPath = FPaths::ProjectDir() + WavPath;
			}
		}

		if (!IFileManager::Get().FileExists(*ResolvedPath))
		{
			UE_LOG(LogTextToSpeech, Error, TEXT("Reference WAV file not found. Tried: %s, %s%s, %s%s"), *WavPath, *FPaths::ProjectContentDir(), *WavPath, *FPaths::ProjectDir(), *WavPath);
			return false;
		}

		UE_LOG(LogTextToSpeech, Log, TEXT("Loading reference WAV from: %s"), *ResolvedPath);

		TArray<uint8> FileBytes;
		if (!FFileHelper::LoadFileToArray(FileBytes, *ResolvedPath) || FileBytes.Num() == 0)
		{
			UE_LOG(LogTextToSpeech, Error, TEXT("Failed to load reference WAV bytes from: %s"), *ResolvedPath);
			return false;
		}

		FWaveModInfo WaveInfo;
		FString ErrorReason;
		if (!WaveInfo.ReadWaveInfo(FileBytes.GetData(), FileBytes.Num(), &ErrorReason))
		{
			UE_LOG(LogTextToSpeech, Warning, TEXT("Invalid reference WAV (%s): %s"), *WavPath, *ErrorReason);
			return false;
		}

		const int32 Channels = WaveInfo.pChannels ? static_cast<int32>(*WaveInfo.pChannels) : 0;
		const int32 SampleRate = WaveInfo.pSamplesPerSec ? static_cast<int32>(*WaveInfo.pSamplesPerSec) : 0;
		const int32 BitsPerSample = WaveInfo.pBitsPerSample ? static_cast<int32>(*WaveInfo.pBitsPerSample) : 0;
		if (Channels <= 0 || SampleRate <= 0 || BitsPerSample != 16 || WaveInfo.SampleDataSize <= 0)
		{
			UE_LOG(LogTextToSpeech, Warning, TEXT("Reference WAV must be PCM16 with valid channels/rate. Channels=%d Rate=%d Bits=%d"), Channels, SampleRate, BitsPerSample);
			return false;
		}

		const int32 TotalPcmSamples = WaveInfo.SampleDataSize / sizeof(int16);
		const int16* Pcm16 = reinterpret_cast<const int16*>(WaveInfo.SampleDataStart);
		if (!Pcm16 || TotalPcmSamples < Channels)
		{
			return false;
		}

		const int32 FrameCount = TotalPcmSamples / Channels;
		TArray<float> Mono;
		Mono.SetNumUninitialized(FrameCount);
		for (int32 Frame = 0; Frame < FrameCount; ++Frame)
		{
			int32 Sum = 0;
			for (int32 Ch = 0; Ch < Channels; ++Ch)
			{
				Sum += static_cast<int32>(Pcm16[Frame * Channels + Ch]);
			}
			Mono[Frame] = static_cast<float>(Sum) / static_cast<float>(Channels) / 32768.0f;
		}

		const int32 TargetSamples = FMath::Max(1, FMath::RoundToInt(static_cast<float>(Mono.Num()) * static_cast<float>(TargetPromptSampleRate) / static_cast<float>(SampleRate)));
		OutMono24k.SetNumUninitialized(TargetSamples);
		for (int32 OutIndex = 0; OutIndex < TargetSamples; ++OutIndex)
		{
			const float SrcPos = (TargetSamples > 1) ? (static_cast<float>(OutIndex) * static_cast<float>(Mono.Num() - 1) / static_cast<float>(TargetSamples - 1)) : 0.0f;
			const int32 I0 = FMath::Clamp(FMath::FloorToInt(SrcPos), 0, Mono.Num() - 1);
			const int32 I1 = FMath::Clamp(I0 + 1, 0, Mono.Num() - 1);
			const float Alpha = SrcPos - static_cast<float>(I0);
			OutMono24k[OutIndex] = FMath::Lerp(Mono[I0], Mono[I1], Alpha);
		}

		const int32 MaxSamples = FMath::Max(1, FMath::RoundToInt(MaxSeconds * static_cast<float>(TargetPromptSampleRate)));
		if (OutMono24k.Num() > MaxSamples)
		{
			OutMono24k.SetNum(MaxSamples, false);
		}

		return OutMono24k.Num() > 512;
	}

	static bool BuildLogMelPromptFeatures(const TArray<float>& Mono24k, TArray<float>& OutFeatures)
	{
		OutFeatures.Reset();
		const int32 FFTSize = 512;
		const int32 HopSize = 256;
		const int32 NumFftBins = FFTSize / 2 + 1;
		if (Mono24k.Num() < FFTSize)
		{
			return false;
		}

		const int32 NumFrames = 1 + (Mono24k.Num() - FFTSize) / HopSize;
		if (NumFrames <= 0)
		{
			return false;
		}

		TArray<float> Window;
		Window.SetNumUninitialized(FFTSize);
		for (int32 n = 0; n < FFTSize; ++n)
		{
			Window[n] = 0.5f - 0.5f * FMath::Cos((2.0f * PI * static_cast<float>(n)) / static_cast<float>(FFTSize - 1));
		}

		TArray<float> MelPointsHz;
		MelPointsHz.SetNumUninitialized(PromptFeatureDim + 2);
		const float MelMin = HzToMel(0.0f);
		const float MelMax = HzToMel(static_cast<float>(TargetPromptSampleRate) * 0.5f);
		for (int32 i = 0; i < PromptFeatureDim + 2; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(PromptFeatureDim + 1);
			MelPointsHz[i] = MelToHz(FMath::Lerp(MelMin, MelMax, t));
		}

		TArray<int32> MelBins;
		MelBins.SetNumUninitialized(PromptFeatureDim + 2);
		for (int32 i = 0; i < PromptFeatureDim + 2; ++i)
		{
			MelBins[i] = FMath::Clamp(FMath::FloorToInt((static_cast<float>(FFTSize + 1) * MelPointsHz[i]) / static_cast<float>(TargetPromptSampleRate)), 0, NumFftBins - 1);
		}

		OutFeatures.SetNumUninitialized(NumFrames * PromptFeatureDim);
		TArray<float> PowerSpec;
		PowerSpec.SetNumUninitialized(NumFftBins);
		float MinEnergy = FLT_MAX, MaxEnergy = -FLT_MAX;

		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			const int32 Start = Frame * HopSize;
			for (int32 k = 0; k < NumFftBins; ++k)
			{
				float Real = 0.0f;
				float Imag = 0.0f;
				for (int32 n = 0; n < FFTSize; ++n)
				{
					const float Sample = Mono24k[Start + n] * Window[n];
					const float Angle = -2.0f * PI * static_cast<float>(k * n) / static_cast<float>(FFTSize);
					Real += Sample * FMath::Cos(Angle);
					Imag += Sample * FMath::Sin(Angle);
				}
				PowerSpec[k] = Real * Real + Imag * Imag;
			}

			for (int32 Mel = 0; Mel < PromptFeatureDim; ++Mel)
			{
				const int32 Left = MelBins[Mel];
				const int32 Center = FMath::Max(MelBins[Mel + 1], Left + 1);
				const int32 Right = FMath::Max(MelBins[Mel + 2], Center + 1);
				float Energy = 0.0f;

				for (int32 k = Left; k < Center; ++k)
				{
					const float W = static_cast<float>(k - Left) / static_cast<float>(Center - Left);
					Energy += PowerSpec[k] * W;
				}
				for (int32 k = Center; k < Right && k < NumFftBins; ++k)
				{
					const float W = static_cast<float>(Right - k) / static_cast<float>(Right - Center);
					Energy += PowerSpec[k] * W;
				}

				const float LogEnergy = FMath::Loge(FMath::Max(Energy, 1.e-9f));
				OutFeatures[Frame * PromptFeatureDim + Mel] = LogEnergy;
				MinEnergy = FMath::Min(MinEnergy, LogEnergy);
				MaxEnergy = FMath::Max(MaxEnergy, LogEnergy);
			}
		}

		UE_LOG(LogTextToSpeech, Warning, TEXT("Prompt log-mel range: [%f, %f]"), MinEnergy, MaxEnergy);
		if (!FMath::IsFinite(MinEnergy) || !FMath::IsFinite(MaxEnergy))
		{
			UE_LOG(LogTextToSpeech, Error, TEXT("Prompt features contain NaN/Inf! MinEnergy=%f MaxEnergy=%f"), MinEnergy, MaxEnergy);
			return false;
		}

		if (MaxEnergy - MinEnergy < 0.1f)
		{
			UE_LOG(LogTextToSpeech, Warning, TEXT("Prompt features have very small dynamic range (%.3f dB), may be silent or invalid"), MaxEnergy - MinEnergy);
		}

		// Normalize log-mel features to [-1, 1] range for consistency with FM decoder expectations
		float GlobalMin = FLT_MAX, GlobalMax = -FLT_MAX;
		for (float f : OutFeatures)
		{
			if (FMath::IsFinite(f))
			{
				GlobalMin = FMath::Min(GlobalMin, f);
				GlobalMax = FMath::Max(GlobalMax, f);
			}
		}
		float Range = GlobalMax - GlobalMin;
		if (Range > 0.1f)
		{
			float Mean = (GlobalMax + GlobalMin) * 0.5f;
			float Scale = 2.0f / FMath::Max(Range, 1e-6f);
			for (float& f : OutFeatures)
			{
				f = (f - Mean) * Scale;
			}
			UE_LOG(LogTextToSpeech, Log, TEXT("Prompt log-mel normalized from [%f, %f] to approximately [-1, 1]"), GlobalMin, GlobalMax);
		}

		return OutFeatures.Num() >= PromptFeatureDim;
	}
}

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
	ProceduralSoundWave->SampleByteSize = sizeof(int16); // QueueAudio data is 16-bit PCM.
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
		TArray<int32> PromptTokens;
		TArray<float> PromptAcousticFeatures;
		const TArray<int32>* PromptTokensPtr = nullptr;
		const TArray<float>* PromptAcousticPtr = nullptr;

		// NOTE: Reference voice conditioning is currently disabled.
		// The LuxTTS model checkpoint provided was trained with zero-prompt conditioning only.
		// Support for variable-length prompt conditioning will be added in a future model update.
		// For now, all synthesis uses zero-prompt (empty prompt) mode.
		
		// DIAGNOSTIC LOG
		FString TokenString;
		for (int32 T : Tokens) TokenString += FString::Printf(TEXT("%d "), T);
		UE_LOG(LogTextToSpeech, Warning, TEXT("AI Received Tokens: %s"), *TokenString);

		// Run the Triple Pipeline
		if (InferenceEngine->RunTextEncoder(Tokens, TTSConfig.SpeechRate, TextFeats, PromptTokensPtr))
		{
			if (InferenceEngine->RunFMDecoder(TextFeats, AcousticFeats, PromptAcousticPtr))
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
						if (FMath::IsFinite(Sample) && FMath::Abs(Sample) > MaxAmplitude) MaxAmplitude = FMath::Abs(Sample);
					}
					
					// This log will tell us if the AI math exploded or stabilized!
					UE_LOG(LogTextToSpeech, Warning, TEXT("AI Peak Amplitude: %f"), MaxAmplitude);

					// Vocoder output can be outside [-1, 1]. Normalize to avoid hard clipping noise.
					const float NormalizationScale = MaxAmplitude > 1.0f ? (1.0f / MaxAmplitude) : 1.0f;

					TArray<int16> PcmAudio;
					PcmAudio.SetNumUninitialized(AudioSamplesFloat.Num());
					
					for (int32 i = 0; i < AudioSamplesFloat.Num(); ++i)
					{
						const float SafeSample = FMath::IsFinite(AudioSamplesFloat[i]) ? AudioSamplesFloat[i] : 0.0f;
						const float ScaledSample = SafeSample * NormalizationScale * TTSConfig.VolumeMultiplier;
						PcmAudio[i] = static_cast<int16>(FMath::Clamp(ScaledSample * 32767.0f, -32768.0f, 32767.0f));
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