#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AudioCapture.h"
#include "HttpModule.h"
#include "WhisperSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranscriptionComplete, const FString&, TranscribedText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTranscriptionFailed, const FString&, ErrorMessage);

/**
 * Manages audio recording and communication with the Whisper STT API.
 */
UCLASS()
class LMINTEGRATION_API UWhisperSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Starts recording audio from the default microphone. */
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StartRecording();

	/** Stops recording and sends the audio to the Whisper API for transcription. */
	UFUNCTION(BlueprintCallable, Category = "Whisper")
	void StopAndTranscribe();

	/** Event fired when transcription is successfully completed. */
	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnTranscriptionComplete OnTranscriptionComplete;

	/** Event fired when transcription fails or an error occurs. */
	UPROPERTY(BlueprintAssignable, Category = "Whisper")
	FOnTranscriptionFailed OnTranscriptionFailed;

private:
	/** Callback for when audio data is captured. */
	void OnAudioCaptureByte(const uint8* AudioData, int32 NumBytes);

	/** Handles the HTTP response from the Whisper API. */
	void OnProcessRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	/** Generates a WAV header for the given PCM data size. */
	TArray<uint8> GenerateWavHeader(int32 DataSize) const;

private:
	/** The audio capture device. */
	FAudioCapture AudioCapture;

	/** Buffer to store raw PCM audio data. */
	TArray<uint8> RecordingBuffer;

	/** Flag to check if we are currently recording. */
	bool bIsRecording;

	/** The sample rate of the capture device. Defaults to 16000 or 44100 depending on device. */
	int32 SampleRate;

	/** The number of channels. usually 1 or 2. */
	int32 NumChannels;

	/** Mutex to protect the recording buffer from concurrent access. */
	FCriticalSection BufferMutex;
};
