// Copyright 2025 Lukas7251. All Rights Reserved.

#include "SpeechToTextLibrary.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/AsyncWork.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "UESTT.h"
#include "Logging/LogMacros.h"
#include "Async/TaskGraphInterfaces.h"
#include "whisper.h"

DEFINE_LOG_CATEGORY_STATIC(LogTranscription, Log, All);

// Global lock for whisper context operations
// Note: whisper.cpp context creation is not thread-safe, and while whisper_full() processing
// may be internally thread-safe per context, we use this lock conservatively during initialization.
// Multiple transcriptions can run simultaneously once each has its own context initialized.
FCriticalSection WhisperContextLock;

TArray<float> ConvertAudioToWhisperFormat(const TArray<uint8>& RawAudioData, int& OutSampleRate, FString& OutErrorMessage)
{
    TArray<float> PCMSamples;
    
    if (RawAudioData.Num() < 44)
    {
        OutErrorMessage = TEXT("Error: Audio file too small or not a valid WAV file.");
        return PCMSamples;
    }
    
    uint32 SampleRate = 0;
    uint16 NumChannels = 0;
    uint16 BitsPerSample = 0;
    
    FMemory::Memcpy(&SampleRate, &RawAudioData[24], 4);
    FMemory::Memcpy(&NumChannels, &RawAudioData[22], 2);
    FMemory::Memcpy(&BitsPerSample, &RawAudioData[34], 2);
    
    OutSampleRate = SampleRate;
    
    int dataOffset = 44;
    int dataSize = RawAudioData.Num() - dataOffset;
    
    bool foundDataChunk = false;
    for (int i = 0; i < RawAudioData.Num() - 8; i++) {
        if (RawAudioData[i] == 'd' && RawAudioData[i+1] == 'a' && RawAudioData[i+2] == 't' && RawAudioData[i+3] == 'a') {
            FMemory::Memcpy(&dataSize, &RawAudioData[i+4], 4);
            dataOffset = i + 8;
            foundDataChunk = true;
            break;
        }
    }
    
    if (!foundDataChunk) {
        OutErrorMessage = TEXT("Error: Could not find data chunk in WAV file.");
        return PCMSamples;
    }
    
    if (dataOffset + dataSize > RawAudioData.Num()) {
        dataSize = RawAudioData.Num() - dataOffset;
    }
    
    if (NumChannels == 0) {
        OutErrorMessage = TEXT("Error: Invalid number of channels in audio file.");
        return PCMSamples;
    }
    
    int bytesPerSample = BitsPerSample / 8;
    if (bytesPerSample == 0) {
        OutErrorMessage = TEXT("Error: Invalid bits per sample in audio file.");
        return PCMSamples;
    }
    
    int numSamples = dataSize / (bytesPerSample * NumChannels);
    PCMSamples.SetNumZeroed(numSamples);
    
    if (BitsPerSample == 16) {
        for (int i = 0; i < numSamples; i++) {
            float sample = 0.0f;
            
            for (int ch = 0; ch < NumChannels; ch++) {
                int sampleOffset = dataOffset + (i * NumChannels + ch) * bytesPerSample;
                
                if (sampleOffset + 1 < RawAudioData.Num()) {
                    const int16* samplePtr = reinterpret_cast<const int16*>(&RawAudioData[sampleOffset]);
                    sample += static_cast<float>(*samplePtr) / 32768.0f;
                }
            }
            
            PCMSamples[i] = sample / NumChannels;
        }
    }
    else if (BitsPerSample == 8) {
        for (int i = 0; i < numSamples; i++) {
            float sample = 0.0f;
            
            for (int ch = 0; ch < NumChannels; ch++) {
                int sampleOffset = dataOffset + (i * NumChannels + ch);
                
                if (sampleOffset < RawAudioData.Num()) {
                    sample += (static_cast<float>(RawAudioData[sampleOffset]) - 128.0f) / 128.0f;
                }
            }
            
            PCMSamples[i] = sample / NumChannels;
        }
    }
    else if (BitsPerSample == 32 && bytesPerSample == 4) {
        for (int i = 0; i < numSamples; i++) {
            float sample = 0.0f;
            
            for (int ch = 0; ch < NumChannels; ch++) {
                int sampleOffset = dataOffset + (i * NumChannels + ch) * bytesPerSample;
                
                if (sampleOffset + 3 < RawAudioData.Num()) {
                    const float* samplePtr = reinterpret_cast<const float*>(&RawAudioData[sampleOffset]);
                    sample += *samplePtr;
                }
            }
            
            PCMSamples[i] = sample / NumChannels;
        }
    }
    else {
        OutErrorMessage = FString::Printf(TEXT("Error: Unsupported audio format: %d bits per sample"), BitsPerSample);
        return TArray<float>();
    }
    
    return PCMSamples;
}

TArray<float> ResampleAudio(const TArray<float>& InputSamples, int SourceSampleRate, int TargetSampleRate)
{
    if (SourceSampleRate <= 0 || TargetSampleRate <= 0 || InputSamples.Num() == 0) {
        return TArray<float>();
    }
    
    if (SourceSampleRate == TargetSampleRate) {
        return InputSamples;
    }
    
    TArray<float> ResampledPCM;
    double ratio = static_cast<double>(SourceSampleRate) / static_cast<double>(TargetSampleRate);
    int targetLength = FMath::CeilToInt(InputSamples.Num() / ratio);
    if (targetLength <= 0) {
        return TArray<float>();
    }
    
    ResampledPCM.SetNumZeroed(targetLength);
    
    for (int i = 0; i < targetLength; i++) {
        double sourceIdx = i * ratio;
        int idx1 = FMath::FloorToInt(sourceIdx);
        int idx2 = FMath::Min(idx1 + 1, InputSamples.Num() - 1);
        double frac = sourceIdx - idx1;
        
        if (idx1 >= 0 && idx1 < InputSamples.Num() && idx2 >= 0 && idx2 < InputSamples.Num()) {
            ResampledPCM[i] = InputSamples[idx1] * (1.0 - frac) + InputSamples[idx2] * frac;
        }
    }
    
    return ResampledPCM;
}

class FTranscriptionTask : public FNonAbandonableTask
{
public:
    FTranscriptionTask(const FString& InAudioFilePath, const FTranscriptionConfig& InConfig, const FOnTranscriptionCompleted& InCompletionCallback)
        : AudioFilePath(InAudioFilePath), CompletionCallback(InCompletionCallback), Config(InConfig)
    {
    }

    static TStatId GetStatId();

    void DoWork() const
    {
        FTranscriptionResult Result = USpeechToTextLibrary::TranscribeAudioInternal(AudioFilePath, Config);
        
        FTranscriptionResult ResultCopy = Result;
        
        FOnTranscriptionCompleted CallbackCopy = CompletionCallback;
        
        FFunctionGraphTask::CreateAndDispatchWhenReady(
            [CallbackCopy, ResultCopy]()
            {
                CallbackCopy.ExecuteIfBound(ResultCopy);
            },
            TStatId(), nullptr, ENamedThreads::GameThread
        );
    }

private:
    FString AudioFilePath;
    FOnTranscriptionCompleted CompletionCallback;
    FTranscriptionConfig Config;
};

TStatId FTranscriptionTask::GetStatId()
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(FTranscriptionTask, STATGROUP_ThreadPoolAsyncTasks);
}

void USpeechToTextLibrary::TranscribeAudioFileAsync(const FString& AudioFilePath, FTranscriptionConfig Config, const FOnTranscriptionCompleted& CompletionCallback)
{
    (new FAutoDeleteAsyncTask<FTranscriptionTask>(AudioFilePath, Config, CompletionCallback))->StartBackgroundTask();
}

FTranscriptionResult USpeechToTextLibrary::TranscribeAudioInternal(const FString& AudioFilePath, const FTranscriptionConfig& Config)
{
    // Start timing
    double StartTime = FPlatformTime::Seconds();
    
    FTranscriptionResult Result;
    Result.bSuccess = false;
    
    if (AudioFilePath.IsEmpty())
    {
        Result.ErrorMessage = TEXT("Audio file path is empty");
        return Result;
    }

    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*AudioFilePath))
    {
        Result.ErrorMessage = FString::Printf(TEXT("Audio file not found: %s"), *AudioFilePath);
        return Result;
    }
    
    FString Extension = FPaths::GetExtension(AudioFilePath).ToLower();
    if (Extension != TEXT("wav"))
    {
        Result.ErrorMessage = FString::Printf(TEXT("Unsupported audio format: %s. Only WAV files are currently supported."), *Extension);
        return Result;
    }
    
    // Determine the active binaries path selected at module startup so we look in the correct folder
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("SpeechToText");
    if (!Plugin.IsValid())
    {
        Result.ErrorMessage = TEXT("Speech to Text plugin not found. Plugin may not be installed correctly.");
        return Result;
    }
    
    FString PluginDir = Plugin->GetBaseDir();
    FSpeechToTextModule& STTModule = FModuleManager::GetModuleChecked<FSpeechToTextModule>("UESTT");
    FString ActiveBinariesPath = STTModule.GetActiveBinariesPath();

    // Fallback order if ActiveBinariesPath is empty (e.g., initialization failure)
    TArray<FString> CandidateDirs;
    if (!ActiveBinariesPath.IsEmpty())
    {
        CandidateDirs.Add(ActiveBinariesPath);
    }
    // Source/ThirdParty layout only
    CandidateDirs.Add(FPaths::Combine(PluginDir, TEXT("Source/ThirdParty/whisper/bin/Win64_GPU")));
    CandidateDirs.Add(FPaths::Combine(PluginDir, TEXT("Source/ThirdParty/whisper/bin/Win64_CPU")));

    FString DllPath;
    for (const FString& Dir : CandidateDirs)
    {
        FString TestPath = FPaths::Combine(Dir, TEXT("whisper.dll"));
        if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*TestPath))
        {
            DllPath = TestPath;
            break;
        }
    }

    if (DllPath.IsEmpty())
    {
        Result.ErrorMessage = TEXT("Whisper DLL not found. Please reinstall the plugin.");
        return Result;
    }
    
    FString ModelDir;
    if (!Config.ModelPath.IsEmpty())
    {
        ModelDir = Config.ModelPath;
        if (!FPaths::FileExists(ModelDir))
        {
            FString ContentDir = IPluginManager::Get().FindPlugin("SpeechToText")->GetContentDir();
            ModelDir = FPaths::Combine(ContentDir, TEXT("STTModel"));
        }
    }
    else
    {
        FString ContentDir = IPluginManager::Get().FindPlugin("SpeechToText")->GetContentDir();
        ModelDir = FPaths::Combine(ContentDir, TEXT("STTModel"));

        if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*ModelDir))
        {
            TArray<FString> ModelFiles;
            IFileManager::Get().FindFiles(ModelFiles, *FPaths::Combine(ModelDir, TEXT("*.bin")), true, false);
            
            if (ModelFiles.Num() > 0)
            {
                ModelDir = FPaths::Combine(ModelDir, ModelFiles[0]);
            }
        }
    }
    
    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelDir))
    {
        Result.ErrorMessage = FString::Printf(TEXT("No speech recognition model found. Please place a model file (*.bin) in your project's %s directory or specify a valid model path."), *ModelDir);
        return Result;
    }
    
    TArray<uint8> RawAudioData;
    if (!FFileHelper::LoadFileToArray(RawAudioData, *AudioFilePath))
    {
        Result.ErrorMessage = TEXT("Failed to load audio file.");
        return Result;
    }
    
    FString ErrorMessage;
    int SampleRate = 0;
    TArray<float> PCMSamples = ConvertAudioToWhisperFormat(RawAudioData, SampleRate, ErrorMessage);
    
    if (!ErrorMessage.IsEmpty())
    {
        Result.ErrorMessage = ErrorMessage;
        return Result;
    }

    if (constexpr int WhisperSampleRate = 16000; SampleRate != WhisperSampleRate) {
        TArray<float> ResampledPCM = ResampleAudio(PCMSamples, SampleRate, WhisperSampleRate);
        
        if (ResampledPCM.Num() == 0) {
            Result.ErrorMessage = TEXT("Failed to resample audio.");
            return Result;
        }
        
        PCMSamples = MoveTemp(ResampledPCM);
    }
    
    struct whisper_context* ctx = nullptr;
    
    {
        FScopeLock Lock(&WhisperContextLock);

        whisper_context_params Cparams = whisper_context_default_params();
        
        FSpeechToTextModule& SpeechToTextModule = FModuleManager::GetModuleChecked<FSpeechToTextModule>("UESTT");
        bool bGPUAvailable = SpeechToTextModule.IsGPUAccelerationAvailable();
        
        if (Config.UseGPU && bGPUAvailable)
        {
            Cparams.use_gpu = true;
            UE_LOG(LogTranscription, Log, TEXT("Transcribing audio using CUDA GPU acceleration"));
        }
        else
        {
            Cparams.use_gpu = false;
            
            if (Config.UseGPU && !bGPUAvailable) {
                UE_LOG(LogTranscription, Log, TEXT("GPU acceleration requested but not available - falling back to CPU"));
            } else {
                UE_LOG(LogTranscription, Log, TEXT("Transcribing audio using CPU only (GPU acceleration disabled)"));
            }
        }
        
        ctx = whisper_init_from_file_with_params(TCHAR_TO_UTF8(*ModelDir), Cparams);
        
        if (ctx == nullptr) {
            Result.ErrorMessage = TEXT("Failed to initialize model.");
            return Result;
        }
    }
    
    whisper_sampling_strategy strategy = Config.SamplingStrategy == ESamplingStrategy::Greedy ? 
        WHISPER_SAMPLING_GREEDY : WHISPER_SAMPLING_BEAM_SEARCH;
    
    struct whisper_full_params params = whisper_full_default_params(strategy);
    
    params.print_realtime = false;
    params.print_progress = false;
    params.print_timestamps = true; // Always enable for segment data
    params.translate = Config.Translate;
    params.no_context = false; // Always use context for better accuracy
    params.token_timestamps = false;
    params.thold_pt = Config.SegmentSensitivity;
    params.max_tokens = 0;
    
    if (!Config.Language.IsEmpty())
    {
        // Validate language code is reasonable (2-3 characters or "auto")
        if (Config.Language != TEXT("auto") && (Config.Language.Len() < 2 || Config.Language.Len() > 3))
        {
            whisper_free(ctx);
            Result.ErrorMessage = FString::Printf(TEXT("Invalid language code: %s. Expected 2-3 character code (e.g., 'en', 'es', 'fr') or 'auto'."), *Config.Language);
            return Result;
        }
        params.language = TCHAR_TO_UTF8(*Config.Language);
    }
    else
    {
        params.language = "auto";
    }
    
    if (Config.Threads > 0)
    {
        // Enforce the same limits as Blueprint meta tags
        params.n_threads = FMath::Clamp(Config.Threads, 1, 32);
    }
    else
    {
        int cores = FPlatformMisc::NumberOfCores();
        params.n_threads = FMath::Clamp(cores, 1, 4);
    }
    
    if (PCMSamples.Num() == 0) {
        whisper_free(ctx);
        Result.ErrorMessage = TEXT("No valid audio data found.");
        return Result;
    }
    
    int result = whisper_full(ctx, params, PCMSamples.GetData(), PCMSamples.Num());
    if (result != 0) {
        whisper_free(ctx);
        Result.ErrorMessage = TEXT("Failed to process audio with whisper.");
        Result.ProcessingTimeSeconds = FPlatformTime::Seconds() - StartTime;
        return Result;
    }
    
    // Capture detected language
    int lang_id = whisper_full_lang_id(ctx);
    if (lang_id >= 0) {
        const char* lang_str = whisper_lang_str(lang_id);
        if (lang_str != nullptr) {
            Result.DetectedLanguage = UTF8_TO_TCHAR(lang_str);
        } else {
            Result.DetectedLanguage = TEXT("unknown");
        }
    } else {
        // Language ID invalid, fallback to config or unknown
        if (!Config.Language.IsEmpty() && Config.Language != TEXT("auto")) {
            Result.DetectedLanguage = Config.Language;
        } else {
            Result.DetectedLanguage = TEXT("unknown");
        }
    }
    
    FString Transcript;
    const int n_segments = whisper_full_n_segments(ctx);
    
    if (n_segments == 0) {
        Transcript = TEXT("No speech detected in audio.");
    } else {
        for (int i = 0; i < n_segments; ++i) {
            if (const char* Segment_Text = whisper_full_get_segment_text(ctx, i)) {
                FString SegmentStr = UTF8_TO_TCHAR(Segment_Text);
                if (!SegmentStr.Contains(TEXT("[SOUND]")) && !SegmentStr.Contains(TEXT("[BLANK_AUDIO]"))) {
                    Transcript += SegmentStr;
                    Transcript += TEXT(" ");
                    
                    // Build segment data with timestamps and confidence
                    FTranscriptionSegment Segment;
                    Segment.Text = SegmentStr.TrimStartAndEnd();
                    Segment.StartTime = whisper_full_get_segment_t0(ctx, i) / 100.0f; // Convert to seconds
                    Segment.EndTime = whisper_full_get_segment_t1(ctx, i) / 100.0f;
                    
                    // Calculate average confidence from segment tokens
                    const int n_tokens = whisper_full_n_tokens(ctx, i);
                    float total_prob = 0.0f;
                    int valid_tokens = 0;
                    for (int j = 0; j < n_tokens; ++j) {
                        float token_prob = whisper_full_get_token_p(ctx, i, j);
                        if (token_prob > 0.0f) {
                            total_prob += token_prob;
                            valid_tokens++;
                        }
                    }
                    Segment.Confidence = valid_tokens > 0 ? total_prob / valid_tokens : 0.0f;
                    
                    Result.Segments.Add(Segment);
                }
            }
        }
    }
    
    if (Transcript.IsEmpty() || Transcript.TrimStartAndEnd().IsEmpty()) {
        Result.Text = TEXT("No speech detected in audio.");
    } else {
        Result.Text = Transcript.TrimStartAndEnd();
    }
    
    Result.bSuccess = true;
    Result.ProcessingTimeSeconds = FPlatformTime::Seconds() - StartTime;
    
    whisper_free(ctx);
    
    return Result;
}