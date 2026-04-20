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
