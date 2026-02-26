// WhisperStreamingThread.cpp
#include "WhisperStreamingThread.h"
#include "SpeechToTextStreamingComponent.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"

FWhisperStreamingThread::FWhisperStreamingThread(struct whisper_context* InCtx, const FTranscriptionConfig& InConfig, USpeechToTextStreamingComponent* InParent)
    : WhisperCtx(InCtx), Config(InConfig), ParentComponent(InParent)
{
    // Create an event flag we can use to wake up the thread
    Semaphore = FPlatformProcess::GetSynchEventFromPool(true);
}

FWhisperStreamingThread::~FWhisperStreamingThread()
{
    if (Semaphore)
    {
        FPlatformProcess::ReturnSynchEventToPool(Semaphore);
        Semaphore = nullptr;
    }
}

bool FWhisperStreamingThread::Init()
{
    return true;
}

// Upgrade PushAudio to calculate volume
void FWhisperStreamingThread::PushAudio(const TArray<float>& NewSamples)
{
    if (NewSamples.Num() == 0) return;

    FScopeLock Lock(&AudioMutex);
    
    float ChunkVolume = CalculateRMS(NewSamples);
    
    // Log to see the microphone volume
    UE_LOG(LogTemp, Warning, TEXT("Mic Volume (RMS): %f"), ChunkVolume);
    
    if (ChunkVolume > Config.VolumeThreshold)
    {
        bHasStartedSpeaking = true;
        CurrentSilenceDuration = 0.0f;
    }
    else if (bHasStartedSpeaking)
    {
        float ChunkDurationInSeconds = (float)NewSamples.Num() / 16000.0f;
        CurrentSilenceDuration += ChunkDurationInSeconds;
    }

    AudioBuffer.Append(NewSamples);
    
    // Track new samples independently of the buffer's total size
    SamplesSinceLastProcess += NewSamples.Num(); 

    if (AudioBuffer.Num() > MaxBufferSize)
    {
        int32 Overflow = AudioBuffer.Num() - MaxBufferSize;
        AudioBuffer.RemoveAt(0, Overflow, EAllowShrinking::No);
    }

    Semaphore->Trigger();
}

// WhisperStreamingThread.cpp

float FWhisperStreamingThread::CalculateRMS(const TArray<float>& Samples)
{
    if (Samples.Num() == 0) return 0.0f;

    float SumSquares = 0.0f;
    for (float Sample : Samples)
    {
        SumSquares += Sample * Sample;
    }
    
    return FMath::Sqrt(SumSquares / Samples.Num());
}

uint32 FWhisperStreamingThread::Run()
{
    while (!bStopThread)
    {
        Semaphore->Wait(100); 
        if (bStopThread) break;

        bool bShouldFinalize = false;
        TArray<float> ProcessingBuffer;

        {
            FScopeLock Lock(&AudioMutex);
            
            if (bHasStartedSpeaking && CurrentSilenceDuration >= Config.MaxSilenceToFinalize)
            {
                bShouldFinalize = true;
                bHasStartedSpeaking = false;
                CurrentSilenceDuration = 0.0f;
            }

            // Use the new tracker instead of AudioBuffer.Num()
            if (!bShouldFinalize && SamplesSinceLastProcess < StepSize)
            {
                continue; // Go back to sleep and wait for more audio
            }

            if (AudioBuffer.Num() == 0) continue;

            ProcessingBuffer = AudioBuffer; 
            
            // Reset the tracker after we copy the buffer
            SamplesSinceLastProcess = 0; 
        }

        // Setup Whisper parameters
        whisper_sampling_strategy strategy = Config.SamplingStrategy == ESamplingStrategy::Greedy ? 
            WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH;
            
        struct whisper_full_params params = whisper_full_default_params(strategy);
        
        params.print_realtime = false;
        params.print_progress = false;
        params.no_context = true; 
        params.single_segment = false; 
        
        params.n_threads = Config.Threads > 0 ? FMath::Clamp(Config.Threads, 1, 32) : FMath::Clamp(FPlatformMisc::NumberOfCores(), 1, 4);
        
        if (Config.Language.IsEmpty() || Config.Language.ToLower() == TEXT("auto"))
        {
            params.language = "en"; 
        }
        else
        {
            params.language = TCHAR_TO_UTF8(*Config.Language);
        }

        // Run the model on the current window of audio
        int result = whisper_full(WhisperCtx, params, ProcessingBuffer.GetData(), ProcessingBuffer.Num());
        
        if (result == 0)
        {
            FString CurrentTranscript;
            const int n_segments = whisper_full_n_segments(WhisperCtx);
            
            for (int i = 0; i < n_segments; ++i)
            {
                const char* segment_text = whisper_full_get_segment_text(WhisperCtx, i);
                if (segment_text)
                {
                    FString SegmentStr = UTF8_TO_TCHAR(segment_text);
                    if (!SegmentStr.Contains(TEXT("[SOUND]")) && !SegmentStr.Contains(TEXT("[BLANK_AUDIO]")))
                    {
                        CurrentTranscript += SegmentStr + TEXT(" ");
                    }
                }
            }

            CurrentTranscript = CurrentTranscript.TrimStartAndEnd();
            
            UE_LOG(LogTemp, Warning, TEXT("Whisper Raw Output: [%s]"), *CurrentTranscript);

            // Route the Transcript
            if (!CurrentTranscript.IsEmpty() && ParentComponent)
            {
                FString SafeTranscript = CurrentTranscript;
                USpeechToTextStreamingComponent* SafeComp = ParentComponent;

                if (bShouldFinalize)
                {
                    AsyncTask(ENamedThreads::GameThread, [SafeComp, SafeTranscript]()
                    {
                        if (IsValid(SafeComp)) { SafeComp->OnFinalTranscriptCompleted.Broadcast(SafeTranscript); }
                    });
                }
                else
                {
                    AsyncTask(ENamedThreads::GameThread, [SafeComp, SafeTranscript]()
                    {
                        if (IsValid(SafeComp)) { SafeComp->OnPartialTranscriptUpdated.Broadcast(SafeTranscript); }
                    });
                }
            }
        }
        
        // 4. If we finalized, clear the buffer AFTER processing is complete
        if (bShouldFinalize)
        {
            FScopeLock Lock(&AudioMutex);
            AudioBuffer.Empty();
            SamplesSinceLastProcess = 0;
        }

        Semaphore->Reset();
    } 
    return 0;
}

void FWhisperStreamingThread::Stop()
{
    bStopThread = true;
    if (Semaphore)
    {
        Semaphore->Trigger(); // Wake it up so it can exit the loop
    }
}