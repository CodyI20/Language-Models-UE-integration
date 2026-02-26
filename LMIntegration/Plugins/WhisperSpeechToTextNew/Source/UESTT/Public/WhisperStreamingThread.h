// WhisperStreamingThread.h
#pragma once

#include "CoreMinimal.h"
#include "SpeechToTextLibrary.h"
#include "HAL/Runnable.h"
#include "whisper.h"

struct FTranscriptionConfig;

class FWhisperStreamingThread : public FRunnable
{
public:
	FWhisperStreamingThread(struct whisper_context* InCtx, const FTranscriptionConfig& InConfig, class USpeechToTextStreamingComponent* InParent);
	virtual ~FWhisperStreamingThread();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;

	// Thread-safe method to push new 16kHz audio from the mic
	void PushAudio(const TArray<float>& NewSamples);

private:
	// --- VAD (Voice Activity Detection) State ---
	float CurrentSilenceDuration = 0.0f;
	bool bHasStartedSpeaking = false;

	// Helper math function
	float CalculateRMS(const TArray<float>& Samples);
	
	struct whisper_context* WhisperCtx;
	FTranscriptionConfig Config;
	USpeechToTextStreamingComponent* ParentComponent;

	// Thread safety
	FThreadSafeBool bStopThread = false;
	FCriticalSection AudioMutex;
	FEvent* Semaphore = nullptr;

	// The Sliding Window Buffer
	TArray<float> AudioBuffer;
    
	// Limits
	const int MaxBufferSize = 16000 * 5; // 5 seconds at 16kHz
	
	// REDUCED: Wake up and process every 400 milliseconds for a "live" feel
	const int StepSize = 16000 * 0.4;
	
	int32 SamplesSinceLastProcess = 0;
};