// SpeechToTextStreamingComponent.cpp
#include "SpeechToTextStreamingComponent.h"
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
    if (StreamingThread != nullptr)
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
    

    // 4. Create and Start the Background Thread
    StreamingThread = new FWhisperStreamingThread(StreamingContext, Config, this);
    RunnableThread = FRunnableThread::Create(StreamingThread, TEXT("WhisperStreamingThread"), 0, TPri_BelowNormal);

    return true;
}

void USpeechToTextStreamingComponent::StopStreaming()
{
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

void USpeechToTextStreamingComponent::ProcessAudioData(const TArray<float>& AudioData)
{
    if (StreamingThread) { StreamingThread->PushAudio(AudioData); }
}
