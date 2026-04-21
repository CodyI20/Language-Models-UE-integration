# Project Scope and Architecture

## Objective

Build a fully local Unreal Engine pipeline where the user's speech is transcribed, interpreted, and translated into NPC behavior/dialogue with optional text-to-speech playback.

## Core constraints from planning

- Unreal Engine target: 5.6.x.

  Note: Unreal Engine versions earlier than 5.3 are unsupported. Versions 5.3 through 5.5 are not the target for this project and have not been confirmed as supported.
- Local-first processing prioritized (offline operation and reduced operational cost).
- VR-oriented performance expectations drove strict latency constraints.
- Avoid large-model overhead when possible; move toward compact, task-specific models.

## Architecture direction

The project evolved toward modular Unreal plugins with clear responsibilities:

- Speech-to-Text plugin for transcription.
- Open microphone plugin for always-on input flow. (VAD)
- Parsing system plugin for semantic command selection.
- Text-to-Speech integrations (multiple approaches explored).
- CLI/API bridge patterns where direct native integration was not immediately available.

## Major design outcomes

- Replaced part of generic LLM command interpretation with lightweight semantic parsing to cut round-trip latency.
- Migrated core systems into reusable plugin form for portability and easier packaging.
- Prioritized explicit asset/model path handling and packaging compatibility after multiple build/package failures.

After multiple failed rebuild attempts, Unreal Engine 5.6.1 was already installed on the development machine (having been used for a prior project) and was adopted as the base engine. All subsequent development took place on UE 5.6.1.

## Initial LLM integration

The first functional integration used the VaRest plugin (obtained via FAB) to send JSON-format HTTP requests from Unreal Engine to a locally running language model. The initial backend was LM Studio with the google-gemma3n model, which ran slowly and had request failures (error code -1).

Switching to Ollama resolved both issues. The model used was gemma3:1b, described as a "tiny" model for low-intensity tasks. With Ollama and a DefaultEngine.ini tweak that reduced HTTP timeout behaviour, responses arrived within 1-2 seconds.

Key Ollama configuration note: a minimum context window of 1024 tokens must be allocated; otherwise the model produces incoherent output. This was configured dynamically in Blueprints using the VaRest "Make JSON" node, with an `options` object containing a `num_ctx` number field.

The 4b+ parameter models were found to be noticeably more reliable and consistent while maintaining the same 1-2 second response window.

## Dual-mode NPC system

An enumerator was introduced to separate two distinct modes of NPC interaction:

- **Commands mode:** The NPC listens to voice input and executes the corresponding animation (e.g., hands up, get on the ground). Originally routed through the LLM, later replaced by the semantic parsing system.
- **Dialogue mode:** The NPC generates and speaks context-appropriate text responses using text-to-speech. This mode can use either of the two approaches (Ollama HTTP routing or Parsing System)

NPC animations were sourced from Mixamo. The mediator layer between the LLM and the animation system originally used a hashmap string lookup, which was later replaced by the ONNX-based semantic parser with the help of the Data Table.

## Plugin architecture overview

Each subsystem lives in its own `.uplugin`. The final plugin structure is:

| Plugin | Role |
|--------|------|
| `SpeechToTextUEAddon` | Whisper-based transcription; wraps whisper.cpp DLLs via a C++ module; handles GPU/CPU binary selection at startup |
| `OpenMicrohponeUEAddon` | Always-on microphone capture via UE AudioCapture; broadcasts raw PCM; VAD threshold-based start/stop logic |
| `ParsingSystemUEAddon` | Semantic command matching using all-MiniLM-L6-v2 ONNX model via NNE; C++ tokenizer wrapper via tokenizers-cpp |
| `CLISystem-Unreal`(external, by getnamo) | Generic subprocess wrapper; base class used for Piper TTS |
| `PiperCLI-Unreal`(external, by getnamo) | Piper neural TTS via CLI process; text in via stdin, raw PCM out, played via USoundWave |
| `UETTS` | LuxTTS plugin using NNE; currently non-functional (outputs noise instead of voice) |
| `RuntimeAudioImporter`(external, by Georgydev) | Runtime audio decoding utility used to feed TTS audio into the UE audio system. Also contains a professional-grade VAD feature |
