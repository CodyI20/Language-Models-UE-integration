# Language Models × Unreal Engine Integration

> **A research and prototype project showcasing the integration of Small/Large Language Models (SLM/LLM) into Unreal Engine 5 for AI-driven NPC interaction.**

The project provides a modular, fully local pipeline that lets an in-game NPC:

_1. **Hear** the player via a live microphone (speech-to-text)_

_2. **Understand** the intent of the spoken command using semantic similarity (SLM inference)_

_3. **Respond** with context-appropriate dialogue and animation_

_4. **Speak back** using on-device neural text-to-speech_


All inference runs locally:
* No cloud API keys required
* No internet connection required.

---

## Supported Engine Version

| Engine | Supported |
|--------|-----------|
| Unreal Engine **5.6** | ✅ Fully supported (Built on it) |
| Unreal Engine 5.3 - 5.5 | ⚠️ Unlikely, but potenitally supported (not tested) |
| Unreal Engine < 5.3 | ❌ (requires NNE plugin, introduced in 5.3) |

---

## Plugin Architecture

The project is composed of loosely coupled Unreal Engine plugins. Each is an independent `*.uplugin` that can be enabled per branch:


* `SpeechToTextUEAddon` — Speech-to-Text (Whisper) (External refactor plugin)

* `OpenMicrohponeUEAddon` — Microphone Capture
> Wraps UE's `AudioCapture` engine module and periodically broadcasts raw PCM data for downstream processing.

* `ParsingSystemUEAddon` — Semantic Command Parsing
> The parser embeds player speech with the SLM and does cosine similarity against pre-cached command alias embeddings to pick the best matching NPC action.

* `CLISystem-Unreal` — Generic CLI Process Wrapper (External plugin)
> Provides a platform-native subprocess handler that launchers an external executable, pipes stdin/stdout, and forwards output to UE. Used as the base class for TTS.

* `PiperCLI-Unreal` — Piper Neural Text-to-Speech (External plugin)
> **Where to find**: [GitHub repository link](https://github.com/getnamo/PiperCLI-Unreal)
> 
> Piper runs as a child process; the component writes text on its stdin and receives raw PCM audio bytes, which are converted to `USoundWave` in-engine.

* `UETTS` — LuxTTS Neural Text-to-Speech (NNE) (Broken)
> Unlike Piper (which uses an external process), UETTS runs inference directly inside UE via the NNE framework.

* `RuntimeAudioImporter` — Runtime Audio Import Utility (External plugin)
> **Where to find**: **Unmaintained** (open-source version); maintained version on [Fab](https://www.fab.com/listings/66e0d72e-982f-4d9e-aaaf-13a1d22efad1) (PAID)
> 
> Provides `UStreamingSoundWave` and VAD support used when feeding live Piper audio into UE's audio system.

---

| Known Limitations | Limitation	Detail |
|--------|-----------|
| Win64 only | STT and Piper TTS plugins declare Win64 PlatformAllowList; macOS/Linux is untested |
| No packaging guide |	PackagingFixes branch addresses some staging issues but documentation is minimal |
| RuntimeAudioImporter OSS unmaintained |	The bundled open-source version may have bugs; the maintained version is on Fab and is paid. Note: It MUST be purchased for professional use |
| External binaries not bundled	| Whisper DLLs, model .bin, Piper Win64 archive, and ONNX models must all be downloaded separately |
| CUDA 12 required for GPU STT development |	GPU DLL set targets cublas64_12 / cudart64_12; older CUDA versions are not supported (for development only) |

## License

This project is licensed under the Apache License 2.0. See LICENSE for the full terms. Third-party components have their own licenses:

- whisper.cpp — MIT  
- all-MiniLM-L6-v2 — Apache 2.0 (via sentence-transformers)  
- Piper TTS — MIT  
- tokenizers-cpp — Apache 2.0  
- CLISystem-Unreal (getnamo) — MIT  
- RuntimeAudioImporter (Georgy Treshchev) — MIT  

## Contributing
There are currently no formal contribution guidelines. If you would like to contribute:

* Fork the repository.  
* Create a feature branch from the most recent relevant branch (e.g., PackagingFixes or LuxTTSBranch).  
* Ensure your changes build cleanly against UE 5.6 on Win64.  
* Open a pull request with a clear description of what was changed and why.

## Video
An explanatory video going in-depth about the process can be found here: [Kaltura Media Saxion](https://media.saxion.nl/media/t/0_6ls0he84)
  
> Note: For bug reports or feature requests, open a GitHub Issue.

