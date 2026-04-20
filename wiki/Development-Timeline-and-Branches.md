# Development Timeline and Branch Strategy

## High-level timeline

- **Early prototype phase (Issues #1–#4):** initial local model dialogue pipeline and iteration on response speed.
- **Speech-to-Text exploration phase:** Whisper and alternatives tested; repeated build/runtime experiments and plugin refactors.
- **Parsing system phase (Issue #24 and follow-ups):** shift from heavyweight command LLM calls to semantic matching with ONNX/NNE.
- **Packaging hardening phase (Issues #30/#31/#32/#45):** fixes for content availability, plugin packaging behavior, and deployment reliability.
- **Text-to-Speech expansion phase (Issues #22/#29/#36/#39/#43/#53/#54):** Piper/Kokoro/Chatterbox/LuxTTS paths explored with trade-off analysis.
- **Documentation consolidation phase (Issue #44):** repository and wiki organization.

## Branch map (purpose-oriented)

- `First-Prototype`, `Second-Prototype`: early conversational proof-of-concept iterations.
- `WhisperBranch`, `SpeechToTextPlugin`, `SpeechToTextPluginRevamp`: STT implementation and stabilization.
- `TextParsingBranch`: parser subsystem implementation and packaging.
- `TextToSpeechBranch`, `PiperBranch`, `LuxTTSBranch`: TTS alternatives and integration experiments.
- `PackagingFixes`: build/package fixes across pluginized systems.
- `main`: curated baseline with README and integration overview.

## Commit-level progression (examples)

- `029abe8` (`Issue #1`): foundational integration iteration.
- `37e3c3f` (`Whisper implementation`): expanded Whisper plugin code paths.
- `0e834fa` (`Issue #24`): parsing addon packaging/config integration updates.
- `301d529` (`Issue #30, #32, #31 - Done`): packaging and model/content adjustments.
- `180dff2` (`Issue #45 - WIP`): STT addon packaging/config and component updates.
- `0c8b363` (`Issue #43 - WIP`), `6089377` (`Issue #53, #54 - WIP`): LuxTTS path iteration.

## Delivery style observed in history

- Work is mostly issue-driven, with branch-specific feature concentration.
- Repeated WIP commits were used for technical exploration before “Done/FIXED” milestones.
- Integration work frequently required parallel updates to plugin config, build files, and runtime component logic.
