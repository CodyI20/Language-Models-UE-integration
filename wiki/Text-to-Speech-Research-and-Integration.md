# Text-to-Speech

## Evaluated engines and paths

- Piper-based approaches (local/offline, strong integration practicality).
- Kokoro-based quality path (higher naturalness, integration complexity trade-offs).
- Chatterbox server bridge (HTTP/API path, quality potential, resource contention concerns).
- LuxTTS plugin path (active implementation research, conversion/runtime challenges).
- ConvAI and other cloud/freemium options considered but not preferred for local-first goals.

## Consistent trade-offs

- Better naturalness often increased integration complexity and/or runtime overhead.
- Server-bridged Python workflows were feasible but introduced latency, packaging, and resource scheduling constraints.
- Fully local plugin approaches reduced deployment dependencies but required substantial C++/runtime integration work.

## Notable technical blockers captured in the plan

- Difficult model format compatibility and conversion pathing (`.bin` vs `.onnx` deployment expectations).
- GPU/CPU resource contention when Unreal and external TTS server workloads ran concurrently.
- Large server+asset footprint concerns for product-size limits.


> TTS remains an area of active experimentation. Integration feasibility is strongly tied to balancing quality, latency, binary size, and maintainability in packaged deployments.

---

## Initial TTS research

The built-in UE text-to-speech voice was used for early NPC dialogue testing but it was extremely robotic being kept only a placeholder. Alternatives:

**Free and open-source:**
- AzSpeech plugin (open-source Microsoft Azure integration, UE 5.3/5.4 only, out of scope)
- Sherpa-ONNX with local models (local TTS, but had access violation errors and API compatibility issues) - Extremely hard to setup

**Cloud / freemium / paid:**
- Microsoft Azure / ElevenLabs (400+ voices, 140+ languages, premium pricing)
- Runtime Text-to-Speech Offline (FAB plugin, 47 languages, real-time synthesis — paid, no GitHub source)
- ReadSpeaker (near-zero latency, real voice actors' voices)

**Shortlisted local options:**
- Piper (local neural TTS, extensive API, MIT licensed archive)
- KokoroTTS (higher naturalness, but fewer integration options at the time)

## Piper TTS (primary integrated path)

Piper was selected as the initial implementation target for these reasons:

1. The archived rhasspy/piper repository is under an MIT license, making it legally compatible with UE's EULA. The current/newer Piper releases use GPL-3.0, which is explicitly incompatible with UE.
2. The paid RuntimeTextToSpeech FAB plugin has no source code on GitHub, ruling it out.
3. A pre-built Unreal Engine CLI integration plugin exists (PiperCLI-Unreal by getnamo), which worked with UE 5.6.1 without modification.

The integration works by running Piper as a child process. The plugin writes text to its stdin and receives raw PCM audio bytes, which are converted to `USoundWave` inside UE for playback. Delay varies from ~0.15 to 0.7 seconds depending on the model used.

The main drawback of Piper's voices is a lack of intonation and emotion.

## Piper vs Kokoro comparison table

| Feature | Kokoro TTS | Piper TTS |
|---------|-----------|-----------|
| Integration method | ONNX runtime plugin | ONNX runtime plugin (CLI process) |
| Voice quality | Studio-quality; expressive; human-like | High-quality; humanized robot voice |
| Model size | ~82MB (.bin + .json) | ~70MB (.onnx + .json) |
| Languages supported | 8 languages | ~50 languages |
| Number of voices | 150 | 900+ |
| Streaming support | Yes | Yes |
| Custom model import | Supported | Supported |
| Multi-speaker support | Limited | Very strong |
| Hardware acceleration | GPU | None; runs on CPU; lightweight |
| Unreal use case | High-fidelity voice output | Lightweight NPC dialogue; multilingual |
| Offline operability | Fully offline | Fully offline |
| Blueprint & C++ support | Full with a proper wrapper | Full with a proper wrapper |
| Platform compatibility | Win, Linux, Android, iOS | Win, Linux, Android, iOS |

## Kokoro TTS attempt

A Python virtual environment was built to create a Kokoro executable. After iterating through multiple build configurations, a working build was produced. The C++ wrapper consisted of an Actor Component that loaded and initialized the model, fed the input string to the `.exe`, and created an audio capture component. Despite no errors appearing in either the wrapper or the `.exe` runtime, no sound played.

## Chatterbox evaluation and server implementation

Chatterbox is an open-source MIT-licensed family of three text-to-speech models. To run it in Python, downgrading to Python 3.11 was required. Initial synthesis speed was ~2 seconds per synthesis via cURL to a Flask server.

The server setup used:

**Command Console (cURL example):**
```
python chatterbox_server.py
curl -X POST http://localhost:5000/tts -H "Content-Type: application/json" -d "{\"text\":\"Ok, this is awesome! [laugh].\"}" --output test_audio.wav
```

The Turbo model supports paralinguistic tags (e.g., `[laugh]`). Other Chatterbox models do not.

For production use, Gunicorn was found to be incompatible with Windows (relies on Unix-specific `fcntl`). Waitress was used instead:
- `pip install waitress`
- `pythonw serve.py` (runs the server without a visible terminal)

An HTML-based client was also created to allow TTS requests from a browser (`http://10.38.94.252:5000/`, intranet-only).

**Major performance blocker:** When both the UE project and the Chatterbox server ran on the same machine, GPU resource contention dropped FPS to unplayable levels. HTTP request processing from within UE took ~7 seconds (later found to be caused by Windows 11 deprioritizing unfocused background windows). These issues caused the Chatterbox on-device bundling approach to be abandoned.

**Size blocker:** The Chatterbox server executable and three voice `.wav` files totalled 5.15 GB. This ruled out on-device bundling entirely.

## Chatterbox: three remaining deployment paths

1. Bundle the `.exe` server with the UE project, starting and killing it along with the application — ruled out due to 5.15 GB size
2. Pre-generate all audio files using Chatterbox, upload them into the UE project, and play them at runtime (current active path)
3. Export Chatterbox's Python logic as ONNX models and integrate via a C++ wrapper inside UE (complex; requires recreating generation logic in C++)

## ONNX export from Python models

The idea of exporting Chatterbox's Python neural networks as ONNX models was explored. The approach is to "trace" each network by feeding fake data so PyTorch can record the mathematical steps into an ONNX file using `torch.onnx.export()`. However, Chatterbox consists of three separate mathematical networks, each of which would require its own ONNX export, and then the entire generation pipeline logic would need to be reimplemented in C++. This was assessed as requiring too much time and advanced C++ expertise.

## MiraTTS, LuxTTS, and Chatterbox comparison

| Engine | Key features | Ease of use | Emotional quality | Cons |
|--------|-------------|-------------|-------------------|------|
| MiraTTS | 100x realtime; 48kHz audio; 100ms latency; 6GB VRAM | Very difficult (Python 3.13, conflicting deps) | Exists | No float16 inference; slow generation (~2 seconds) |
| LuxTTS | 150x realtime; 48kHz audio; 1GB VRAM; voice cloning | Workable (Python 3.12) | Good but sometimes inaccurate intonation | Slow (similar to Piper CLI) |
| Chatterbox | 3 models; paralinguistic tags; exaggeration tuning; multi-language; low VRAM | Workable (Python 3.11) | Human-like (especially Turbo) | 2 seconds per synthesis; GPU conflict with UE |

## LuxTTS plugin implementation

LuxTTS was selected as the most promising local ONNX-based path due to:
- Pre-built ONNX model files available on HuggingFace
- Runs on 1GB VRAM (viable for Meta Quest)
- Claims of 150x real-time speed

The `UETTS` plugin was created with the following C++ class structure:

- `UTextToSpeechComponent` — Actor Component; calls `Speak(FString)`; manages `USoundWaveProcedural` and `UAudioComponent` for playback
- `FLuxTTSTokenizer` — wraps text-to-token conversion
- `FLuxTTSInference` — loads ONNX model via NNE and runs inference
- `FLuxTTSConfig` struct — exposes `SpeechRate`, `VolumeMultiplier`, optional reference voice settings

The vocoder provided by LuxTTS was a `.bin` file (PyTorch binary weight file), not an ONNX model. A conversion was required using `torch.onnx.export()`. The conversion succeeded (the NNE plugin initialized the ONNX file without errors), and audio output was detected (amplitude > 0), but the output was pure noise rather than speech.

On `.bin` vs `.onnx` file types:

- `.bin` in this context is a generic binary containing trained PyTorch neural network weights (not directly portable).
- `.onnx` is a standardized model file containing the neural network graph, weights, and computation logic, deployable in any environment.

Direct conversion from `.bin` to `.onnx` is not possible. It must be done by loading the weights from `.bin` via Python and running `torch.onnx.export()`, which traces the computation graph with fake input data.

After weeks of iteration and a meeting with the project supervisor, development on the LuxTTS plugin was halted. Issue #43 was closed as `wontfix` on 20.04.2026, with the comment: "All TTS-related files are in ONNX format and were integrated into the UE editor via NNE. The output sound is pure noise." The conversion script (`VocoderONNXExport.py`) is attached to the issue for reference.

## RuntimeAudioImporter plugin

The open-source RuntimeAudioImporter plugin (by Georgy Treshchev) was added to provide `UStreamingSoundWave` support for feeding live Piper audio into UE's audio system. Its files did not appear in Git due to a missing `.gitattributes` and `.gitignore` setup inside the plugin folder. This was however later fixed

Note: The open-source version is unmaintained. The maintained version on Fab is paid and must be purchased for professional use.

## Wontfix TTS issues summary

| Issue | Title | Reason closed as wontfix |
|-------|-------|--------------------------|
| #15 | Stream the speech to and transcription from the whisper API | GPU fix (#33) made STT fast enough; streaming no longer needed |
| #28 | Text-to-speech implementation with Kokoro-TTS | Could not produce audio despite no errors |
| #38 | TTS voice choice option exposed in the editor | Deprioritized; not currently viable |
| #41 | Pre-generate audio files for dialogue options with Chatterbox | Deprioritized in favour of other paths |
| #43 | LuxTTS integration into Unreal Engine | Output was noise; halted after weeks of work |
| #54 | TTS: Procedural Audio Generation | Dependent on #43; closed together |
