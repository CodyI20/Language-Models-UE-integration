# Text-to-Speech Research and Integration

## Evaluated engines and paths

- Piper-based approaches (local/offline, strong integration practicality).
- Kokoro-based quality path (higher naturalness, integration complexity trade-offs).
- Chatterbox server bridge (HTTP/API path, quality potential, resource contention concerns).
- LuxTTS plugin path (active implementation research, conversion/runtime challenges).
- ConvAI and other cloud/freemium options considered but not preferred for local-first goals.

## Consistent trade-offs observed

- Better naturalness often increased integration complexity and/or runtime overhead.
- Server-bridged Python workflows were feasible but introduced latency, packaging, and resource scheduling constraints.
- Fully local plugin approaches reduced deployment dependencies but required substantial C++/runtime integration work.

## Notable technical blockers captured in the plan

- Difficult model format compatibility and conversion pathing (`.bin` vs `.onnx` deployment expectations).
- GPU/CPU resource contention when Unreal and external TTS server workloads ran concurrently.
- Large server+asset footprint concerns for product-size limits.

## Issue-linked status signals

- `#22`, `#29`, `#36`, `#39`: progressive TTS integration and dependency exploration.
- `#43` (LuxTTS): closed as `wontfix` / not planned.
- `#53`, `#54`: late-stage LuxTTS/procedural generation work logged as WIP and later deprioritized.

## Practical interpretation

TTS remains an area of active experimentation. Integration feasibility is strongly tied to balancing quality, latency, binary size, and maintainability in packaged deployments.
