# Speech-to-Text

## Evaluated directions

- Whisper-based local transcription (primary technical path).
- Wit.ai / Meta Voice SDK intent+transcription route (cloud-dependent alternative).
- Sphinx/plugin-style keyword approaches (low-latency but insufficient semantic reliability for full requirements).

## Key points

- Local Whisper path has strong control and privacy benefits but requires significant setup effort (model/runtime/libraries/build integration).
- Streaming STT was investigated to reduce latency, but reliability and microphone capture consistency were recurring blockers.
- Packaging introduced additional path/dependency issues that did not appear during editor-only runs.
- CUDA/GPU acceleration was explored and succesfully implemented

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
