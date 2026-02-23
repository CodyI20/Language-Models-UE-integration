// SpeechToTextStreamingComponent.cpp
#include "SpeechToTextStreamingComponent.h"
#include "Voice.h"
#include "TimerManager.h"
#include "WhisperStreamingThread.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "UESTT.h"

// Local lock for streaming initalization
FCriticalSection StreamingInitLock;

USpeechToTextStreamingComponent::USpeechToTextStreamingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void USpeechToTextStreamingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopStreaming();
    Super::EndPlay(EndPlayReason);
}

bool USpeechToTextStreamingComponent::StartStreaming(FTranscriptionConfig Config)
{
    if (VoiceCapture.IsValid() || StreamingThread != nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("Streaming is already active."));
        return false;
    }

    // 1. Resolve the Model Path (Matching your SpeechToTextLibrary.cpp logic)
    FString ModelPath = Config.ModelPath;
    if (ModelPath.IsEmpty())
    {
        // Default models directory relative to project
        FString ProjectModelsDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Content/STTModels")));
        TArray<FString> ModelFiles;
        
        if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*ProjectModelsDir))
        {
            IFileManager::Get().FindFiles(ModelFiles, *FPaths::Combine(ProjectModelsDir, TEXT("*.bin")), true, false);
            if (ModelFiles.Num() > 0)
            {
                ModelPath = FPaths::Combine(ProjectModelsDir, ModelFiles[0]);
            }
        }
    }

    if (!FPaths::FileExists(ModelPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Streaming failed: Model file not found at %s"), *ModelPath);
        return false;
    }

    // 2. Initialize Whisper Context
    {
        FScopeLock Lock(&StreamingInitLock);
        struct whisper_context_params cparams = whisper_context_default_params();
        
        FSpeechToTextModule& STTModule = FModuleManager::GetModuleChecked<FSpeechToTextModule>("UESTT");
        if (Config.UseGPU && STTModule.IsGPUAccelerationAvailable())
        {
            cparams.use_gpu = true;
        }
        else
        {
            cparams.use_gpu = false;
        }
        
        StreamingContext = whisper_init_from_file_with_params(TCHAR_TO_UTF8(*ModelPath), cparams);
    }

    if (!StreamingContext)
    {
        UE_LOG(LogTemp, Error, TEXT("Streaming failed: Could not initialize whisper context."));
        return false;
    }

    // 3. Start the Voice Capture
    VoiceCapture = FVoiceModule::Get().CreateVoiceCapture("");
    if (!VoiceCapture.IsValid() || !VoiceCapture->Start())
    {
        whisper_free(StreamingContext); // Clean up on failure
        StreamingContext = nullptr;
        UE_LOG(LogTemp, Warning, TEXT("Aborting because Voice capture is invalid!"));
        return false;
    }

    // 4. Create and Start the Background Thread
    StreamingThread = new FWhisperStreamingThread(StreamingContext, Config, this);
    RunnableThread = FRunnableThread::Create(StreamingThread, TEXT("WhisperStreamingThread"), 0, TPri_BelowNormal);

    // 5. Start the audio polling timer
    GetWorld()->GetTimerManager().SetTimer(
        AudioCaptureTimerHandle, 
        this, 
        &USpeechToTextStreamingComponent::CaptureAudioTick, 
        0.1f, // 100ms
        true
    );

    return true;
}

void USpeechToTextStreamingComponent::StopStreaming()
{
    // 1. Stop the polling timer
    GetWorld()->GetTimerManager().ClearTimer(AudioCaptureTimerHandle);

    // 2. Stop microphone capture
    if (VoiceCapture.IsValid())
    {
        VoiceCapture->Stop();
        VoiceCapture = nullptr;
    }

    // 3. Stop and destroy the thread cleanly
    if (StreamingThread)
    {
        StreamingThread->Stop(); // Signals the while-loop to break
        
        if (RunnableThread)
        {
            RunnableThread->WaitForCompletion(); // Block until thread actually finishes
            delete RunnableThread;
            RunnableThread = nullptr;
        }

        delete StreamingThread;
        StreamingThread = nullptr;
    }

    // 4. Free the Whisper context memory
    if (StreamingContext)
    {
        whisper_free(StreamingContext);
        StreamingContext = nullptr;
    }
}

void USpeechToTextStreamingComponent::CaptureAudioTick()
{
    if (!VoiceCapture.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("NO VOICE CAPTURE!"));
        return;
    }

    uint32 AvailableVoiceData = 0;
    EVoiceCaptureState::Type CaptureState = VoiceCapture->GetCaptureState(AvailableVoiceData);

    if (CaptureState == EVoiceCaptureState::Ok && AvailableVoiceData > 0)
    {
        TArray<uint8> RawVoiceData;
        RawVoiceData.SetNumUninitialized(AvailableVoiceData);

        uint32 OutAvailableVoiceData = 0;
        VoiceCapture->GetVoiceData(RawVoiceData.GetData(), AvailableVoiceData, OutAvailableVoiceData);
        UE_LOG(LogTemp, Warning, TEXT("Captured %d bytes of audio from mic."), OutAvailableVoiceData);

        // Convert the 16-bit PCM byte array to float array (-1.0f to 1.0f) for Whisper
        int32 NumSamples = OutAvailableVoiceData / 2; // 2 bytes per 16-bit sample
        TArray<float> FloatSamples;
        FloatSamples.SetNumUninitialized(NumSamples);

        const int16* SamplePtr = reinterpret_cast<const int16*>(RawVoiceData.GetData());
        for (int32 i = 0; i < NumSamples; ++i)
        {
            // Divide by 32768.0f to normalize, exactly as done in your ConvertAudioToWhisperFormat function
            FloatSamples[i] = static_cast<float>(SamplePtr[i]) / 32768.0f; 
        }

        // TODO: Push FloatSamples to your FWhisperStreamingThread's thread-safe buffer
        if (StreamingThread) { StreamingThread->PushAudio(FloatSamples); }
    }
}