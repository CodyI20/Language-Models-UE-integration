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
    
    // 1. Calculate the volume of this new specific chunk
    float ChunkVolume = CalculateRMS(NewSamples);
    
    // 2. Determine if the user is speaking
    if (ChunkVolume > Config.VolumeThreshold)
    {
        bHasStartedSpeaking = true;
        CurrentSilenceDuration = 0.0f; // Reset silence timer
    }
    else if (bHasStartedSpeaking)
    {
        // We calculate duration based on the number of samples. 
        // 16000 samples = 1 second.
        float ChunkDurationInSeconds = (float)NewSamples.Num() / 16000.0f;
        CurrentSilenceDuration += ChunkDurationInSeconds;
    }

    // 3. Append to our sliding window
    AudioBuffer.Append(NewSamples);

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
        // Sleep until PushAudio triggers the semaphore, checking at least every 100ms
        Semaphore->Wait(100);
        
        if (bStopThread) break;

        TArray<float> ProcessingBuffer;
        {
            FScopeLock Lock(&AudioMutex);
            // Only process if we have a meaningful chunk of audio (e.g., 1 second)
            UE_LOG(LogTemp, Warning, TEXT("Buffer Size: %d / %d"), AudioBuffer.Num(), StepSize);
            if (AudioBuffer.Num() < StepSize)
            {
                continue; // Go back to sleep
            }
            ProcessingBuffer = AudioBuffer; // Copy the buffer for safe processing
        }

        // Setup Whisper parameters exactly like your file-based implementation
        whisper_sampling_strategy strategy = Config.SamplingStrategy == ESamplingStrategy::Greedy ? 
            WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH;
            
        // Force greedy sampling for maximum speed during real-time streaming
        struct whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        
        params.print_realtime = false;
        params.print_progress = false;
        
        // Disable context to prevent hallucinating past words in short buffers
        params.no_context = true; 
        
        // Allow multiple segments if the buffer gets longer than a few words
        params.single_segment = false; 
        
        // CRITICAL FOR SPEED: Do not use "auto" language detection during streaming.
        // It will cause massive stuttering. Default to "en" if empty or auto.
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

            // 1. Evaluate VAD State BEFORE broadcasting
            bool bShouldFinalize = false;
            {
                FScopeLock Lock(&AudioMutex);
                if (bHasStartedSpeaking && CurrentSilenceDuration >= Config.MaxSilenceToFinalize)
                {
                    bShouldFinalize = true;
                    
                    // Reset states for the next sentence
                    bHasStartedSpeaking = false;
                    CurrentSilenceDuration = 0.0f;
                    AudioBuffer.Empty(); // Clear the buffer so the next sentence starts fresh
                }
            }

            // 2. Route the Transcript
            if (!CurrentTranscript.IsEmpty() && ParentComponent)
            {
                FString SafeTranscript = CurrentTranscript;
                USpeechToTextStreamingComponent* SafeComp = ParentComponent;

                if (bShouldFinalize)
                {
                    // The user has paused. Lock in the Final text!
                    AsyncTask(ENamedThreads::GameThread, [SafeComp, SafeTranscript]()
                    {
                        if (IsValid(SafeComp)) { SafeComp->OnFinalTranscriptCompleted.Broadcast(SafeTranscript); }
                    });
                }
                else
                {
                    // The user is still talking. Update the Partial text!
                    AsyncTask(ENamedThreads::GameThread, [SafeComp, SafeTranscript]()
                    {
                        if (IsValid(SafeComp)) { SafeComp->OnPartialTranscriptUpdated.Broadcast(SafeTranscript); }
                    });
                }
            }
        }
        
        Semaphore->Reset();
    } // End of while(!bStopThread) loop
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