// SpeechToTextStreamingComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpeechToTextLibrary.h" // For FTranscriptionConfig
#include "SpeechToTextStreamingComponent.generated.h"

// Delegates for real-time updates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPartialTranscriptUpdated, const FString&, PartialText);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFinalTranscriptCompleted, const FString&, FinalText);

class FWhisperStreamingThread;
struct whisper_context;
class FRunnableThread;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class UESTT_API USpeechToTextStreamingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpeechToTextStreamingComponent();
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "Speech to Text|Streaming")
	bool StartStreaming(FTranscriptionConfig Config);

	UFUNCTION(BlueprintCallable, Category = "Speech to Text|Streaming")
	void StopStreaming();
	
	UFUNCTION(BlueprintCallable, Category = "Speech to Text|Streaming")
	void ProcessAudioData(const TArray<float>& AudioData);

	UPROPERTY(BlueprintAssignable, Category = "Speech to Text|Streaming")
	FOnPartialTranscriptUpdated OnPartialTranscriptUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Speech to Text|Streaming")
	FOnFinalTranscriptCompleted OnFinalTranscriptCompleted;

private:
	// The actual system thread running our FRunnable
	FRunnableThread* RunnableThread = nullptr;
	
	// The loaded Whisper model context for this streaming session
	struct whisper_context* StreamingContext = nullptr;
	
	FWhisperStreamingThread* StreamingThread = nullptr;
};