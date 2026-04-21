# Speech-to-Text

## Evaluated directions

- Whisper-based local transcription (primary technical path).
- Wit.ai / Meta Voice SDK intent+transcription route (cloud-dependent alternative).
- Sphinx/plugin-style keyword approaches (low-latency but insufficient semantic reliability for full requirements).

## Key points

- Local Whisper path has strong control and privacy benefits but requires significant setup effort (model/runtime/libraries/build integration).
- Streaming STT was investigated to reduce latency, but reliability and microphone capture consistency were recurring blockers.
- Packaging introduced additional path/dependency issues that did not appear during editor-only runs.
- CUDA/GPU acceleration was explored and successfully implemented.

## Performance notes

- Early STT cycles could be multiple seconds end-to-end.
- Progressively improved toward near-1-second range in favorable cases after plugin/code/model optimization.
- Open microphone mode introduced voice activity detection threshold challenges in noisy environments.

## Key implementation and packaging concerns

- Runtime model discovery and deterministic file paths were essential for packaged builds.
- Plugin content inclusion and third-party binary availability required explicit handling.
- Missing CUDA DLLs and related runtime assumptions were believed to be a deployment risk.

## Current status

- STT capability is functionally advanced but requires careful environment and packaging validation.
- For production-grade reliability, pathing/dependency checks and fallback behavior should remain first-class concerns.

---

## Evaluated options comparison (06.02.2026)

The following table was produced during the initial speech-to-text research phase:

| Feature | Runtime Speech Recognizer | Meta Voice SDK (Wit.ai) | Windows Speech Recognition |
|---------|--------------------------|------------------------|---------------------------|
| Connection | Offline | Online (Cloud) | Offline |
| Accuracy | Excellent (Whisper API) | High | Moderate |
| Cost | One-time plugin fee | Free | Free |
| Platform | PC, Mobile, Consoles, VR | Quest, PC | Windows Only |

## Runtime Speech Recognizer (Whisper-based, primary path)

The most popular offline solution at the time. It uses OpenAI Whisper technology processed locally with no internet required and no API costs.

Key features:
- Supports 95+ languages
- Works cross-platform (Windows, Android, iOS)
- GPU acceleration on Windows via Vulkan

Implementation was complicated by the plugin's publicly archived (unmaintained since February 2025) GitHub version, which required manual effort to resolve ggml third-party source issues and UE C11 file incompatibilities.

## Meta Voice SDK / Wit.ai (cloud alternative, rejected)

Evaluated as a single-model alternative that could combine speech-to-text and intent extraction. Pros and cons found during testing:

| Pros | Cons |
|------|------|
| One model for speech-to-text and intent | Runs on the cloud (internet mandatory) |
| Trained on specific intents, more accurate | Still not fast enough |
| Lightweight | Takes time to train properly |
| Easy to implement via API | Wit.ai app training has bugs |
| Easy cURL communication | No official UE SDK for 5.6+ |
| Client/server key security | Crashes after a few minutes of use |
| | Very little documentation or community info for UE |
| | Many unresolved issues on the Wit.ai GitHub page |

A community workaround was found to build the plugin for UE 5.6.1. The sample project provided by the Wit.ai developers performed poorly and crashed repeatedly. Wit.ai was deprioritized as a result.

## Sphinx plugin (phonetic keyword approach, rejected)

A lightweight phonetic dictionary plugin was tested. It worked for single- or two-word inputs but was unreliable for full sentences. It was discarded as a primary input mechanism.

## Custom whisper.cpp integration from scratch

Multiple approaches to building whisper.cpp with CMake were tried:

- Compiling static libraries with CMake then linking to both a custom plugin and the RuntimeSpeechRecognizer plugin — failed
- Following a blog-based implementation guide — failed due to library incompatibilities
- Building with CUDA enabled using: -failed since CUDA wasn't properly setup, therefore not used when building the .dlls

```
cmake -DGGML_CUDA=ON -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON ..
cmake --build . --config Release -j 8
```

The resulting `.lib` and `.dll` files are placed under `ThirdParty/whisper` inside the plugin. The CUDA build requires:

- CUDA Toolkit from NVIDIA: https://developer.nvidia.com/cuda-downloads (requires a supporting driver version - can be checked in the terminal with `nvidia-smi`)

Additionally, the feature requires a ggml model file from HuggingFace (e.g. `ggml-base.bin` or `ggml-base.en.bin`). Quantized models (`q`) are smaller and faster, and generally work great, even with CUDA. A highly specific model was used to ensure the best output from performance, speed and result quality standpoints (`ggml-tiny.en-q8_0.bin` - An INT8 quantized English tiny model).

## Plugin binary selection logic

The `SpeechToTextUEAddon` plugin (`UESTT.cpp`) selects GPU or CPU binaries at startup by checking for two locations in order:

1. `Source/ThirdParty/whisper/bin/Win64_GPU` — CUDA-accelerated path (`ggml-cuda.dll`, `cublas64_12.dll`, `cublasLt64_12.dll`, `cudart64_12.dll`, `nvJitLink_120_0.dll`)
2. `Source/ThirdParty/whisper/bin/Win64_CPU` — CPU fallback path (`ggml.dll`, `ggml-base.dll`, `ggml-cpu.dll`, `whisper.dll`)

If neither path is loadable, a dialog is shown to the user and the plugin reports as unavailable. The `bGPUAccelerationAvailable` flag is set accordingly.

## CUDA GPU acceleration issue and resolution

For a substantial portion of development, the plugin silently fell back to CPU because the properly-built CUDA DLLs were missing from the ThirdParty directory. The issue was confirmed using the open-source [Dependencies tool](https://github.com/lucasg/Dependencies), which showed `cudart64_12.dll` and `cublas64_12.dll` as unresolved. Additionally, a crash was observed when running GPU acceleration in an otherwise empty UE scene:

```
[2026.03.27-13.57.25:322][275]LogTranscription: Transcribing audio using CUDA GPU acceleration
[2026.03.27-13.57.27:583][402]LogWindows: FPlatformMisc::RequestExit(1, WindowsPlatformCrashContext.AbortHandler)
[2026.03.27-13.57.27:583][402]LogCore: Engine exit requested (reason: Win RequestExit)
```

This was resolved by adding the full, properly built CUDA DLL set: `cublas64_12.dll`, `cublasLt64_12.dll`, `cudart64_12.dll`, and `nvJitLink_120_0.dll` to the `Win64_GPU` ThirdParty folder. CUDA 12 (specifically the cublas64_12 and cudart64_12 variant) is required for GPU mode.

## Why Whisper Streaming was marked wontfix

Whisper streaming was evaluated early as a way to reduce latency. It used the "greedy" decoding algorithm and proved less accurate than the .wav file path. It also took longer to produce results. Eventually, after the CUDA-accelerated .dlls were fixed, which brough the transcription time to ~0.2 seconds, streaming officially became redundant.

## Open microphone VAD implementation

The Open Microphone plugin captures audio using UE's `AudioCapture` module and broadcasts raw PCM data. A voice activity detection (VAD) threshold is used to determine when recording should start and stop. The current implementation reads the current loudness of the environment:

- High ambient noise requires a higher dB threshold to prevent background noise from triggering transcription
- Quiet environments require a lower dB threshold so the user's voice is not missed

This makes automatic threshold calibration important for deployment in varied real-world environments.
