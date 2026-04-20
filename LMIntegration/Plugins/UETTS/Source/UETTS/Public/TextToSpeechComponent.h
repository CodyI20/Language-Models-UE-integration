// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TextToSpeechComponent.generated.h"

class USoundWaveProcedural;
class FLuxTTSTokenizer;
class FLuxTTSInference;

USTRUCT(BlueprintType)
struct FLuxTTSConfig
{
	GENERATED_BODY()

	// Can be expanded later if LuxTTS accepts other parameters
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS Configuration")
	float SpeechRate = 1.0f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS Configuration")
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS Configuration|Reference Voice")
	bool bUseReferenceVoice = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS Configuration|Reference Voice")
	FString ReferenceVoiceWavPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS Configuration|Reference Voice")
	FString ReferenceTranscript;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TTS Configuration|Reference Voice", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float MaxReferenceSeconds = 3.0f;
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class UETTS_API UTextToSpeechComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	UTextToSpeechComponent();
	
	UFUNCTION(BlueprintCallable, Category = "Text To Speech")
	void Speak(const FString& TextToSpeak);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text To Speech")
	FLuxTTSConfig TTSConfig;
	
	virtual void BeginPlay() override;

private:
	// Internal Audio Component to play the generated voice
	UPROPERTY()
	UAudioComponent* VoiceAudioComponent;

	// Procedural Sound Wave to feed real-time audio data into
	UPROPERTY()
	USoundWaveProcedural* ProceduralSoundWave;
	
	// Handles text-to-integer conversion for the ONNX model
	TSharedPtr<FLuxTTSTokenizer> Tokenizer;
	
	TSharedPtr<FLuxTTSInference> InferenceEngine;
};