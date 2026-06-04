
#include "SpeechToTextLibrary.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Async/AsyncWork.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "UESTT.h"
#include "Logging/LogMacros.h"
#include "Async/TaskGraphInterfaces.h"
#include "whisper.h"
#include "Core/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogTranscription, Log, All);

static void WhisperLogCallback(ggml_log_level level, const char * text, void * user_data)
{
    if (!text) return;
    FString LogText = UTF8_TO_TCHAR(text);
    LogText.TrimStartAndEndInline();
    
    if (LogText.IsEmpty()) return;
    
    if (level == GGML_LOG_LEVEL_ERROR) {
        UE_LOG(LogTranscription, Error, TEXT("%s"), *LogText);
    } else if (level == GGML_LOG_LEVEL_WARN) {
        UE_LOG(LogTranscription, Warning, TEXT("%s"), *LogText);
    } else {
        UE_LOG(LogTranscription, Log, TEXT("%s"), *LogText);
    }
}

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
    
    // Ensures file data bounds are not exceeded
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
    PCMSamples.SetNumUninitialized(numSamples);
    
    const float ChannelMultiplier = 1.0f/static_cast<float>(NumChannels);
    
    if (BitsPerSample == 16) {
        constexpr float NormalizationFactor = 1.0f/32768.0f;
        for (int i = 0; i < numSamples; i++) {
            float Sum = 0.0f;
            for (int ch = 0; ch < NumChannels; ch++)
            {
                int16 SampleValue = 0;
                // Safely copy the 2 bytes regardless of memory alignment on ARM
                int ByteIndex = dataOffset + ((i * NumChannels + ch) * sizeof(int16));
                FMemory::Memcpy(&SampleValue, &RawAudioData[ByteIndex], sizeof(int16));
                
                Sum += static_cast<float>(SampleValue) * NormalizationFactor;
            }
            
            PCMSamples[i] = Sum * ChannelMultiplier;
        }
    }
    else if (BitsPerSample == 8) {
        const uint8* SampleData = &RawAudioData[dataOffset];
        constexpr float NormalizationFactor = 1.0f/128.0f;
        for (int i = 0; i < numSamples; i++) {
            float Sum = 0.0f;
            for (int ch = 0;ch < NumChannels; ch++)
            {
                Sum += (static_cast<float>(*SampleData++) - 128.0f) * NormalizationFactor;
            }
            
            PCMSamples[i] = Sum * ChannelMultiplier;
        }
    }
    else if (BitsPerSample == 32 && bytesPerSample == 4) {
        for (int i = 0; i < numSamples; i++) {
            float Sum = 0.0f;
            for (int ch = 0; ch < NumChannels; ch++)
            {
                float SampleValue = 0.0f;
                // Safely copy the 4 bytes regardless of memory alignment on ARM
                int ByteIndex = dataOffset + ((i * NumChannels + ch) * sizeof(float));
                FMemory::Memcpy(&SampleValue, &RawAudioData[ByteIndex], sizeof(float));
                
                Sum += SampleValue;
            }
            PCMSamples[i] = Sum * ChannelMultiplier;
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
    
    ResampledPCM.SetNumUninitialized(targetLength);
    
    const float* InData = InputSamples.GetData();
    float* OutData = ResampledPCM.GetData();
    const int InputMaxIdx = InputSamples.Num() - 1;
    
    for (int i = 0; i < targetLength; i++) {
        double sourceIdx = i * ratio;
        int idx1 = static_cast<int>(sourceIdx);
        int idx2 = FMath::Min(idx1 + 1, InputMaxIdx);
        float frac = static_cast<float>(sourceIdx - idx1);
        
        float SampleA = InData[idx1];
        float SampleB = InData[idx2];
        
        OutData[i] = SampleA + frac * (SampleB - SampleA);
    }
    
    return ResampledPCM;
}

void USpeechToTextLibrary::TranscribeAudioFileAsync(const FString& AudioFilePath, FTranscriptionConfig Config, const FOnTranscriptionCompleted& CompletionCallback)
{
    // Allocate a pointer to our thread pointer on the heap so we can pass it safely to both lambdas
    FThread** ThreadPtr = new FThread*(nullptr);

    // Create the thread. 
    // Because this lambda is defined inside a member function of USpeechToTextLibrary, 
    // it inherently has access to USpeechToTextLibrary's protected/private methods
    *ThreadPtr = new FThread(
        TEXT("WhisperTranscriptionThread"),
        [ThreadPtr, AudioFilePath, Config, CompletionCallback]()
        {
            // Background thread transcription
            FTranscriptionResult Result = USpeechToTextLibrary::TranscribeAudioInternal(AudioFilePath, Config);

            // Result sent back to the game thread
            AsyncTask(ENamedThreads::GameThread, [ThreadPtr, Callback = CompletionCallback, FinalResult = MoveTemp(Result)]()
            {
                // Callback trigger
                Callback.ExecuteIfBound(FinalResult);

                // Cleanup
                if (*ThreadPtr)
                {
                    (*ThreadPtr)->Join();
                    delete *ThreadPtr;
                }
                
                delete ThreadPtr;
            });
        },
        8 * 1024 * 1024, // 8 MB Stack Size specifically to prevent Android SIGSEGV crashes
        TPri_Normal
    );
}

FTranscriptionResult USpeechToTextLibrary::TranscribeAudioInternal(const FString& AudioFilePath, const FTranscriptionConfig& Config)
{
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
    
#if PLATFORM_WINDOWS
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("SpeechToText");
    if (!Plugin.IsValid())
    {
        Result.ErrorMessage = TEXT("Speech to Text plugin not found. Plugin may not be installed correctly.");
        return Result;
    }
    
    FString PluginDir = Plugin->GetBaseDir();
    FSpeechToTextModule& STTModule = FModuleManager::GetModuleChecked<FSpeechToTextModule>("UESTT");
    FString ActiveBinariesPath = STTModule.GetActiveBinariesPath();

    // Fallback order if ActiveBinariesPath is empty
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
#endif //PLATFORM_WINDOWS
    
    FString ContentDir = IPluginManager::Get().FindPlugin("SpeechToText")->GetContentDir();
    FString ModelDir = FPaths::Combine(ContentDir, TEXT("STTModel"));
    
    if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*ModelDir))
    {
        TArray<FString> ModelFiles;
        IFileManager::Get().FindFiles(ModelFiles, *FPaths::Combine(ModelDir, TEXT("*.bin")), true, false);
            
        if (ModelFiles.Num() > 0)
        {
            ModelDir = FPaths::Combine(ModelDir, ModelFiles[0]);
        }
    }
    
    if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*ModelDir))
    {
        Result.ErrorMessage = FString::Printf(TEXT("No speech recognition model found. Please place a model file (*.bin) in your project's %s directory or specify a valid model path."), *ModelDir);
        return Result;
    }

    // Load the model into memory safely so it works inside packaged APKs
    TArray<uint8> ModelBuffer;
    if (!FFileHelper::LoadFileToArray(ModelBuffer, *ModelDir))
    {
        Result.ErrorMessage = TEXT("Failed to load model file into memory. Ensure it packaged correctly.");
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
    
    RawAudioData.Empty();
    
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
            ULog::Info(TEXT("SpeechToTextLibrary.cpp - TranscribeAudioInternal"), TEXT("Transcribing audio using CUDA GPU acceleration"));
        }
        else
        {
            Cparams.use_gpu = false;
            
            if (Config.UseGPU && !bGPUAvailable) {
                ULog::Warning(TEXT("SpeechToTextLibrary.cpp - TranscribeAudioInternal"), TEXT("GPU acceleration requested but not available - falling back to CPU"));
            } else {
                ULog::Warning(TEXT("SpeechToTextLibrary.cpp - TranscribeAudioInternal"), TEXT("Transcribing audio using CPU only (GPU acceleration disabled)"));
            }
        }
        // Captures all internal whisper/ggml errors
        whisper_log_set(WhisperLogCallback,nullptr);
        
        // Read from the memory buffer instead of the raw file path
        ctx = whisper_init_from_buffer_with_params(ModelBuffer.GetData(), ModelBuffer.Num(), Cparams);
        
        if (ctx == nullptr) {
            Result.ErrorMessage = TEXT("Failed to initialize model from memory buffer.");
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
    params.no_context = false; // Using context = better accuracy
    params.token_timestamps = false;
    params.thold_pt = Config.SegmentSensitivity;
    params.max_tokens = 0;
    
    FTCHARToUTF8 ConvertedLanguage(*Config.Language);
    
    if (!Config.Language.IsEmpty())
    {
        if (Config.Language != TEXT("auto") && (Config.Language.Len() < 2 || Config.Language.Len() > 3))
        {
            whisper_free(ctx);
            Result.ErrorMessage = FString::Printf(TEXT("Invalid language code: %s. Expected 2-3 character code (e.g., 'en', 'es', 'fr') or 'auto'."), *Config.Language);
            return Result;
        }
        
        params.language = ConvertedLanguage.Get(); 
    }
    else
    {
        params.language = "auto";
    }
    
    if (Config.Threads > 0)
    {
        params.n_threads = FMath::Clamp(Config.Threads, 1, 32);
    }
    else
    {
        int cores = FPlatformMisc::NumberOfCores();
        params.n_threads = FMath::Clamp(cores, 1, 8);
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
    
    if (n_segments > 0) {
        // Pre-allocating memory to avoid runtime reallocations
        Result.Segments.Reserve(Result.Segments.Num() + n_segments);
        
        // Assuming there are around 50 chars per segment
        Transcript.Reserve(Transcript.Len() + n_segments*50);
        
        for (int i = 0; i < n_segments; ++i) {
            if (const char* Segment_Text = whisper_full_get_segment_text(ctx, i)) {
                if (!strstr(Segment_Text, "[SOUND]") && !strstr(Segment_Text, "[BLANK_AUDIO]")) {
                    FString SegmentStr = UTF8_TO_TCHAR(Segment_Text);
                    SegmentStr.TrimStartAndEndInline();
                    if (SegmentStr.IsEmpty())
                    {
                        continue;
                    }
                    
                    Transcript += SegmentStr;
                    Transcript += TEXT(" ");
                    
                    // Build segment data with timestamps and confidence
                    FTranscriptionSegment& Segment = Result.Segments.Emplace_GetRef();
                    Segment.Text = MoveTemp(SegmentStr);
                    Segment.StartTime = whisper_full_get_segment_t0(ctx, i) * 0.01f; // Convert to seconds
                    Segment.EndTime = whisper_full_get_segment_t1(ctx, i) * 0.01f;
                    
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
                }
            }
        }
    }
    
    Transcript.TrimStartAndEndInline();
    
    if (Transcript.IsEmpty()) {
        Result.Text = TEXT("No speech detected in audio.");
    } else {
        Result.Text = MoveTemp(Transcript);
    }
    
    Result.bSuccess = true;
    Result.ProcessingTimeSeconds = FPlatformTime::Seconds() - StartTime;
    
    whisper_free(ctx);
    
    return Result;
}