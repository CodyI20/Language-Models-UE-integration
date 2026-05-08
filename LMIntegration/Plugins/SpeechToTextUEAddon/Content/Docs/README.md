# SpeechToText UE Addon

UESTT is an Unreal Engine runtime plugin for asynchronous speech-to-text processing.
It takes user audio input (WAV files), processes it using a local Whisper AI model, and returns the transcribed text along with detailed timestamp and confidence segment data.
The plugin is designed for dictation, voice command interpretation, and accessibility workflows.

Internally, the system combines:

- A **Whisper AI model** via ggml/whisper.cpp
- An **Audio Capture Component** to record local player microphone input
- A **CUDA GPU acceleration** path with an automatic CPU fallback
- Automatic **PCM audio resampling** to 16kHz for model compatibility

---

## What the plugin does

At a high level, the speech-to-text pipeline:

1. Loads the ggml, whisper, and CUDA DLLs at startup depending on hardware availability.
2. Records the player's microphone input to a temporary `.wav` file.
3. Loads a `.bin` Whisper model file from the plugin content directory.
4. Converts the raw WAV audio into an array of normalized floats.
5. Transcribes the audio asynchronously on a dedicated background thread.
6. Produces a `FTranscriptionResult` containing the text, segment data, and language detected.

---

## Where the important content lives

Plugin content and points of interest in the code:

- `Plugins/SpeechToText/Source/ThirdParty/whisper/bin/Win64_GPU/` and `Win64_CPU/`
  - Precompiled Whisper and GGML DLLs, including CUDA libraries.
- `Plugins/SpeechToText/Content/STTModel/`
  - Directory where the Whisper model `.bin` files should be placed.
- `Plugins/SpeechToText/Input/IMC_SpeechToText.IMC_SpeechToText`
  - Default Enhanced Input mapping context for triggering recordings.
- `ProjectSavedDir()/STT_Recordings/` - This is usually in the `users` folder.
  - The default directory where generated `.wav` audio files are stored.

---

## Setup

### 1) Enable the plugin dependencies

`UESTT` relies on several standard and external plugins:

- `AudioCapture` (UE-native)
- `AudioMixer` (UE-native)
- `EnhancedInput` (UE-native)
- `UTLogger` (External plugin - REQUIRED)

### 2) Make sure the content is cooked

The project packaging config automatically stages the plugin content and third-party binaries. This includes the `.dll` files for both the CPU and GPU paths. This is crucial because the module loads these binaries dynamically at runtime.

### 3) Verify the Whisper model

The plugin requires a valid Whisper AI model to function.
Ensure you place a `.bin` model file into the `Content/STTModel` directory.

### 4) Initialize the module before use

The `FSpeechToTextModule` automatically handles the initialization and DLL loading at engine startup. No manual module initialization is required, provided the DLLs are correctly placed.

### 5) Prepare the component

To record player audio, add a `USpeechToTextComponent` to the player character.
It automatically adds a deferred `UAudioCaptureComponent` and binds the required mapping context on `BeginPlay()`.

---

## Important variables and what they do

`FTranscriptionConfig` contains the main tuning values for transcription.
These settings heavily affect performance and accuracy.

### Performance Settings

- `UseGPU`
  - Toggles CUDA GPU acceleration if the hardware supports it.
- `Threads`
  - Number of CPU threads to allocate for transcription. Leaving it at `0` allows for auto-detection (1-8 threads).
- `SamplingStrategy`
  - Enum containing `Greedy` (Fastest) or `BeamSearch` (More Accurate).

### Output Settings

- `Language`
  - The 2-3 character language code (e.g., "en", "es"). Setting this to "auto" tells Whisper to automatically detect the language.
- `Translate`
  - If true, directly translates the detected non-English speech into English text. (It doesn't work the other way around)

### Advanced Settings

- `SegmentSensitivity`
  - Controls silence detection sensitivity (0.01 to 1.0). Lower values create more segmented text batches.

### Transcription result fields

`FTranscriptionResult` tracks the final output. These are the fields to inspect when the process completes:
- `bSuccess`: True if transcription finished without errors.
- `Text`: The full, combined transcribed string.
- `Segments`: An array of `FTranscriptionSegment` structs providing per-phrase `Text`, `StartTime`, `EndTime`, and `Confidence`.
- `DetectedLanguage`: The recognized language code.
- `ProcessingTimeSeconds`: Time taken to run the inference.

---

## How to use it in a game or application

### Blueprint usage (example can be found in the demo)

1. Get the `USpeechToTextComponent` attached to your actor.
2. Call `StartRecording` to begin capturing microphone input.
3. Call `StopRecording` when the player finishes.
4. Use the `Speech to Text` asynchronous node, passing the generated audio file path and your `FTranscriptionConfig`.
5. Pull from the `CompletionCallback` execution pin to access the `FTranscriptionResult` data.

### C++ usage

1. Include `"SpeechToTextLibrary.h"`.
2. Configure your `FTranscriptionConfig` struct.
3. Call `USpeechToTextLibrary::TranscribeAudioFileAsync(AudioFilePath, Config, CompletionCallback)`.
4. The callback will execute securely on the Game Thread.

### Example runtime pattern

Start recording via push-to-talk → Stop recording and save WAV to disk → Asynchronously process audio via `TranscribeAudioFileAsync` → Extract the text from `Result.Text` → Use the text wherever needed.

### Recommended integration habits

- Keep `Language` set to `auto` unless you are exclusively developing for a single language.
- Use `Greedy` sampling for faster, real-time command input.
- Limit `Threads` manually if auto-detect causes lag or instability on lower-end hardware.

---

## Demo setup

To quickly test recording functionality, ensure the `IMC_SpeechToText` input mapping context is present in your project. The component automatically binds this for the local player controller upon initialization.

---

## Calibration and testing

You can evaluate the system's efficiency by monitoring the `ProcessingTimeSeconds` and `Confidence` values from the `FTranscriptionResult` struct.
If transcriptions are taking too long, verify that GPU acceleration was successfully loaded by checking your log output, or try swapping your `.bin` model file for a smaller variant (e.g., changing from a `small` or `base` model down to a `tiny` model - However, keep in mind that it decreases accuracy).

---

## Troubleshooting

### 1) Fails to load whisper DLLs

If the module displays a failure dialog on engine boot, verify that the Visual C++ Redistributable for Visual Studio 2019 or newer is installed on the machine.

### 2) No speech recognition model found

Check that you have explicitly placed a `.bin` file in the `Content/STTModel/` directory.

### 3) Audio file path INACCESSIBLE!

If you see this error in the log, verify that the `Saved/STT_Recordings/` directory was successfully created and the application has write permissions.

### 4) Packaging issues

If transcription fails in a packaged build, ensure that the CUDA (`cudart64_12.dll`, `cublas64_12.dll`) and GGML binaries in the `Source/ThirdParty/` folder were correctly staged by the build script.

---

## Notes for maintainers

A few implementation details worth remembering when updating this plugin:

- `FSpeechToTextModule` directly handles iterating through directory paths to load and unload DLL handles (managing both CUDA and CPU fallback binaries).
- Transcription execution is deliberately pushed to `EAsyncExecution::Thread` (a dedicated background thread) instead of the standard thread pool to avoid `cuBLAS` stack overflow issues.
- Context initialization in whisper is protected by a global `FCriticalSection WhisperContextLock`, as `whisper_init_from_file_with_params` is not thread-safe.
- Audio parsing manually accounts for varying sample rates, completely resampling the buffer to Whisper's mandatory 16kHz format using linear interpolation.
