# Development Timeline and Branch Strategy

## High-level timeline

- **Early prototype phase:** Initial local model dialogue pipeline and iteration on response speed.
- **Speech-to-Text exploration phase:** Whisper and alternatives tested; repeated build/runtime experiments and plugin refactors.
- **Parsing system phase:** Shift from heavyweight command LLM calls to semantic matching with ONNX/NNE.
- **Packaging hardening phase:** Fixes for content availability, plugin packaging behavior, and deployment reliability.
- **Text-to-Speech expansion phase:** Piper/Kokoro/Chatterbox/LuxTTS paths explored with trade-off analysis.
- **Documentation consolidation phase:** Repository and wiki organization.

## Branch map (purpose-oriented)

- `First-Prototype`, `Second-Prototype`: Early conversational proof-of-concept iterations.
- `WhisperBranch`, `SpeechToTextPlugin`, `SpeechToTextPluginRevamp`: Different attempts at STT implementation.
- `TextParsingBranch`: Parser subsystem implementation and packaging.
- `TextToSpeechBranch`, `PiperBranch`, `LuxTTSBranch`: TTS alternatives and integration experiments.
- `PackagingFixes`: Build/package fixes across pluginized systems.
- `main`: Curated baseline with README and integration overview. (TO UPDATE: Will have the most up-to-date version upon finishing the internship)

## Commit-level progression (examples)

- `029abe8` (`Issue #1`): Foundational integration iteration.
- `37e3c3f` (`Whisper implementation`): Expanded Whisper plugin code paths.
- `0e834fa` (`Issue #24`): Parsing addon packaging/config integration updates.
- `301d529` (`Issue #30, #32, #31 - Done`): Packaging and model/content adjustments.
- `180dff2` (`Issue #45 - WIP`): STT addon packaging/config and component updates.
- `0c8b363` (`Issue #43 - WIP`), `6089377` (`Issue #53, #54 - WIP`): LuxTTS path iteration.

---

## Detailed process log

##### Engine setup and first LLM integration

> Failed attempts to build Unreal Engine 5.7.2 from source code caused by two plugins that could not be compiled. After multiple failed attempts, UE 5.6.1 (already installed) was adopted. The VaRest plugin was downloaded from FAB to handle JSON communication with the LLM backend. LM Studio with google-gemma3n was the initial model, but it failed requests.
> 
> After adjusting settings, the model began answering correctly.  <img width="430" height="1206" alt="image" src="https://github.com/user-attachments/assets/31608567-2134-4356-9f56-7f5e144e6126" />

##### Ollama switch and GitHub board setup

> The GitHub Projects planning board was set up and all known tasks were added to it. Ollama was tried as an alternative to LM Studio and worked immediately without any configuration issues. The Unreal Engine blueprint code was updated to produce a JSON payload compatible with Ollama's API format. Model used: `gemma3:1b`. Response time: 1-2 seconds. `DefaultEngine.ini` was updated to prevent long HTTP timeouts.

##### Speech-to-text research begins

> Three STT options were evaluated: Runtime Speech Recognizer (Whisper, offline), Meta Voice SDK / Wit.ai (cloud, intent-aware), and Windows Speech Recognition (offline, Windows only). A comparison table was produced (see the Speech-to-Text page). The LLM communication logic was moved to an Actor Component, reducing response time to ~700ms.

##### Whisper.cpp build attempts

> The GeorgyDev RuntimeSpeechRecognizer plugin failed to build due to ggml file incompatibilities. A co-worker assisted but the problem could not be resolved. Work then shifted to researching a from-scratch whisper.cpp integration using CMake. A working compilation path was found using CMake.

##### CMake/CUDA build attempts for whisper

> CUDA toolkit was obtained. Multiple approaches tried:
> 
> - Compiling static libraries with CMake and linking to both custom and RuntimeSpeechRecognizer plugins — failed.
> - Implementing whisper following a blog guide — failed due to library issues.
> 
> A phonetic dictionary plugin (Sphinx) was found and tested as an alternative.

##### Sphinx plugin and FAB discovery

> Sphinx plugin was tested but proved unreliable for sentences longer than one or two words. Building RuntimeSpeechRecognizer with CMake/CUDA also continued to fail (linker errors due to architectural differences). [A FAB plugin](https://github.com/lukas7251/SpeechToText-UnrealEngine) that integrates whisper.cpp directly was found and tested. It worked but initial transcription times were around 4 seconds.

##### Full pipeline timing measurement

> The Whisper FAB plugin was wired into the full LLM pipeline. Color-coded print statements showed the total AI time (speech capture → transcription → LLM response). The result confirmed that the combined latency was not a viable solution for real-time VR interaction. <img width="597" height="313" alt="image" src="https://github.com/user-attachments/assets/c41d91a8-d203-47d4-846e-22cd6320890f" />
 
##### Supervisor and teacher feedback
> 
> Circle meeting with teachers yielded two directives:
> 
> 1. Use Whisper streaming to get closer to real-time feedback.
> 2. Investigate Meta's Wit.ai, which could combine speech-to-text and intent extraction into a single model.
> 
> Wit.ai setup started: a Wit.ai app was created and utterances were trained. Terminal queries returned only the transcribed text with no intent results despite successful training.

##### Wit.ai sample project testing and rejection

> The sample project from the wit.ai developers was tested and found to crash constantly after a few minutes. Conclusions: poor sample quality, cloud dependency, crashes, and no official UE 5.6+ support (fix via a forum workaround). Wit.ai was deprioritized and at this time the focus shifted to NPC animation: a Mixamo character and animations were added, and a hashmap mediator layer was created to map LLM string output to animation actions.

##### Streaming whisper attempt and NPC animations

> Attempted whisper streaming (whisper plugin moved from Engine to Project folder to allow recompilation). Streaming was not stable: greedy algorithm produced inaccurate partial results and took longer than .wav-based transcription. NPC animations using Mixamo assets were implemented; the mediator layer mapped LLM responses to animation sequences.

##### Blueprint cleanup and dialogue mode

> LanguageModelCommunicationComponent and the character blueprint were reorganized into Functions and collapsed graphs. Local variables and node reroutes improved readability. The Dialogue mode was added: the NPC now speaks responses using UE's built-in TTS (robotic voice). The Commands/Dialogue enum was introduced.

##### Open microphone prototype

> Open microphone (VAD-based, no press-and-hold needed) was implemented. It was integrated into the streaming path and later decoupled into its own plugin. VAD worked by monitoring the audio level against a configurable dB threshold. The transcription speed was still ~2.5 seconds for the .wav path. Streaming was explored further.

##### Open microphone decoupled; LLM dialogue prompt testing

> Open microphone logic was moved to its own plugin. Supervisor meeting directed next steps: test at least 5 Ollama models for NPC dialogue coherence. A prompt was crafted for a 4b-qt model and verified to work well both inside Ollama and in UE. Whisper model was switched to `ggml-tiny.en-q8_0.bin`, reducing transcription to ~1 second. A new Mixamo animation for `ACTION_HANDSBACK` was added.

##### Multi-model LLM dialogue testing

> Six models were tested with a fixed context length of 256k tokens and an identical instruction prompt defining the NPC as a civilian character. Results:
> 
> | Model | Result |
> |-------|--------|
> | Gemma3:270m | Cannot reason; results unreliable despite fast speed (~1 second) |
> | Qwen3:0.6b | Uses a "thinking" phase before answering; slightly better than 270m but fails with novel phrasing |
> | Gemma3:1b | Better reasoning but refuses roleplaying due to fiction/reality conflict |
> | Gemma3:4b | DNF |
> | Qwen3:4b | DNF |
> | Llama3:8b | DNF |
> | Gemma3:12b | DNF |
> 
> The models 4b+ were confirmed to be the minimum viable size for stable dialogue quality.

##### TTS voice research begins

> Research into TTS voice alternatives started. Solutions identified: AzSpeech (UE 5.3/5.4 only), Sherpa-ONNX (local, but requires careful setup), Microsoft Azure, ElevenLabs, ReadSpeaker, and the paid RuntimeTextToSpeech plugin (around $400 for professional use). Three candidates were shortlisted: RuntimeAudioImporter, Piper, and KokoroTTS.

##### Open microphone converted to GameSubsystem

> The Open Microphone plugin was refactored from an Actor Component to a Game Subsystem. A whisper plugin crash was investigated and resolved via a full reinstall. Plugin DLLs and static libraries were force-added to Git to enable version rollback.

##### Parsing system concept

> New idea arose: replace the Ollama-based command routing with a script that parses the Whisper transcription directly for keywords. This was the starting idea that grew into the full semantic parsing system.

##### Parsing system first implementation (ONNX/NNE)

> The idea of a simple hashmap keyword parser was evaluated and rejected (fragile, hardcoded, fails on paraphrase). The replacement architecture used all-MiniLM-L6-v2 (Xenova/all-MiniLM-L6-v2), a 22.7M parameter sentence-transformer ONNX model, run directly inside UE via the NNE plugin. This removed the need for an external Ollama/LM Studio process entirely for the Commands path.

##### Parsing system bug fixes and enum refactor

> A `void*` cast bug was fixed (`static_cast<void*>` and `static_cast<uint32>` on tensor shape). The output type was changed from FString to the `ENPCAnimationID` enum, making the codebase less error-prone and more editor-friendly. The async wrapper (`AsyncGetBestMatchingCommand`) was added to keep calculations off the main thread. A pre-defined dialogue line array was added to the Data Table, from which a random line is selected based on the recognized command.

##### Parser plugin migration

> The parsing system was migrated into its own plugin (`ParsingSystemUEAddon`), including all C++ code, ThirdParty tokenizer libraries, and the ONNX model asset. Packaging was verified in a separate C++ UE project.

##### STT plugin improvements

> The whisper model was added to the plugin's own Content folder. Automatic path resolution was implemented. The SpeechToTextComponent was created. Most logic was ported to C++.

##### STT performance peak

> After optimizing the C++ whisper wrapper and switching to a higher-quantized model, average transcription time reached ~0.84 seconds.

##### Piper TTS initial research

> The rhasspy/piper archived repository was selected over the newer GPL-3.0 version due to MIT licensing compatibility with UE's EULA. A CLI-based UE plugin for Piper (PiperCLI-Unreal) was found and integrated successfully. Piper voices were noted as high-quality but robotic (no emotion/intonation). Delay varies from ~0.15 to 0.7 seconds based on the output dialogue line.

##### Kokoro TTS attempt

> A Python virtual environment was used to build a Kokoro executable. The C++ wrapper was written but no audio played despite no errors. The RuntimeTextToSpeech FAB plugin was found to support both Piper .onnx and Kokoro .bin models.

##### MiraTTS, LuxTTS, Chatterbox evaluation

> Three new engines evaluated: MiraTTS (Python 3.13, conflicting dependencies, could not run), LuxTTS (Python 3.12, ran successfully in Python, no C++ API), Chatterbox (Python 3.11, MIT licensed, 3 models in one, localhost server approach, ~2 seconds per synthesis). The Chatterbox server was reached via cURL HTTP POST requests from UE via VaRest (plugin which allows accessing HTTP & Json nodes from Blueprints)

##### Chatterbox server integration and packaged build failures

> Chatterbox HTTP integration via VaRest was attempted. Two major problems: 7-second processing time (later attributed to Windows 11 deprioritizing unfocused windows), and GPU contention dropping FPS to unplayable levels. The Waitress WSGI server was set up. A consultant (Mike, senior software engineer) suggested testing with a headless UE server build; this exposed broken plugin paths in packaged builds. Missing CUDA DLLs were confirmed: `cudart64_12.dll` and `cublas64_12.dll`.

##### GPU acceleration fix (and great breakthrough

> The CUDA DLL set (`cublas64_12.dll`, `cublasLt64_12.dll`, `cudart64_12.dll`, `nvJitLink_120_0.dll`) was added to the `Win64_GPU` ThirdParty directory after being properly built from the [whisper.cpp repository](https://github.com/ggml-org/whisper.cpp) with CMAKE using CUDA Toolkit v12.8. The plugin now correctly selects GPU DLLs when available, falling back to CPU otherwise.

##### Packaging bug fixes

> Plugin Content folders were fixed to be available in packaged builds. Chatterbox Python dependencies were re-locked via `uv`. RuntimeAudioImporter Git visibility was corrected.

##### Chatterbox server size rejection

> The bundled Chatterbox server `.exe` and three voice `.wav` files totalled 5.15 GB, making on-device bundling infeasible. Server-only or pre-generation approaches remained viable alternatives.

##### STT plugin production test

> The packaged STT plugin was tested in a blank UE C++ project.

##### LuxTTS plugin implementation

> The LuxTTS plugin (`UETTS`) was created with a `UTextToSpeechComponent`, a `FLuxTTSTokenizer`, and a `FLuxTTSInference` class. The vocoder's `.bin` file was converted to `.onnx` using `torch.onnx.export()`. The NNE plugin successfully loaded the ONNX vocoder but produced only noise instead of speech.

##### LuxTTS work halted

> After several weeks of work, audio output was still noise (amplitude > 0 but unintelligible). Focus returned to improving existing features.
