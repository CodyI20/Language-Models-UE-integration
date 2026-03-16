
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "SpeechToTextLibrary.generated.h"

class FTranscriptionTask;

USTRUCT(BlueprintType)
struct FTranscriptionSegment
{
    GENERATED_BODY()
    
    /** The transcribed text content for this segment. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    FString Text;
    
    /** Start time of the segment in seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    float StartTime = 0.0f;
    
    /** End time of the segment in seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    float EndTime = 0.0f;
    
    /** Confidence score (0.0 to 1.0) representing the reliability of the transcription. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    float Confidence = 0.0f;
};

USTRUCT(BlueprintType)
struct FTranscriptionResult
{
    GENERATED_BODY()
    
    /** Whether the transcription process completed successfully. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    bool bSuccess = false;
    
    /** The full transcribed text. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    FString Text;
    
    /** Array of individual speech segments with timestamps. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    TArray<FTranscriptionSegment> Segments;
    
    /** The language code detected or used (e.g., "en", "fr"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    FString DetectedLanguage;
    
    /** Time taken to process the audio in seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    float ProcessingTimeSeconds = 0.0f;
    
    /** Error description if the operation failed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text")
    FString ErrorMessage;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnTranscriptionCompleted, const FTranscriptionResult&, Result);

UENUM(BlueprintType)
enum class ESamplingStrategy : uint8
{
    Greedy UMETA(DisplayName = "Greedy (Fastest)"),
    BeamSearch UMETA(DisplayName = "Beam Search (More Accurate)")
};

USTRUCT(BlueprintType)
struct FTranscriptionConfig
{
    GENERATED_BODY()
    
    // Core Settings
    
    /** Language code (e.g., "en", "es", "auto"). Leave empty for auto-detection. 
     * Replace the reflection macro with UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text|Core") to enable
     */
    UPROPERTY()
    FString Language;
    
    // Performance Settings
    
    /** Whether to use CUDA GPU acceleration if available. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text|Performance")
    bool UseGPU = true;
    
    /** Number of CPU threads to use. 0 = Auto-detect (1-8 threads). 
     * If it causes instability / lag set a custom value e.g (4)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text|Performance", meta = (ClampMin = "0", ClampMax = "32"))
    int32 Threads = 0;
    
    /** Sampling strategy: Greedy (faster) or Beam Search (more accurate). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text|Performance")
    ESamplingStrategy SamplingStrategy = ESamplingStrategy::Greedy;
    
    // Output Settings
    
    /** If true, translates the speech to English. 
     * Replace the reflection macro with UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text|Output") to enable
     */
    UPROPERTY()
    bool Translate = false;
    
    // Advanced Settings
    
    /** Controls silence detection sensitivity (0.01-1.0). Lower values create more segments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speech to Text|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SegmentSensitivity = 0.01f;
};

UCLASS()
class UESTT_API USpeechToTextLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Asynchronously transcribes an audio file to text using the Whisper AI model.
     * 
     * @param AudioFilePath Path to the WAV file to transcribe.
     * @param Config Configuration options for the transcription process.
     * @param CompletionCallback Delegate called when transcription is finished.
     */
    UFUNCTION(BlueprintCallable, Category = "Speech to Text", meta = (DisplayName = "Speech to Text"))
    static void TranscribeAudioFileAsync(const FString& AudioFilePath, FTranscriptionConfig Config, const FOnTranscriptionCompleted& CompletionCallback);

protected:
    friend class FTranscriptionTask;
    static FTranscriptionResult TranscribeAudioInternal(const FString& AudioFilePath, const FTranscriptionConfig& Config);
};