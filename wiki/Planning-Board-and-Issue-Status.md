# Planning Board and Issue Status

This page summarizes planning signals visible through issue tracking and the development log.

## Current issue snapshot (repository-wide)

- Total issues observed: 52
- Open: 10
- Closed: 42

Most-used labels:

- `enhancement`
- `investigation`
- `bug`
- `research`
- `documentation`

## Explicit `wontfix` / not planned items

The following were marked as non-delivery targets and should be treated as excluded for current scope:

- `#15` Stream Whisper API speech/transcription path.
- `#28` Kokoro-TTS integration.
- `#38` Editor-exposed TTS voice selection option.
- `#41` Pre-generated Chatterbox dialogue audio path.
- `#43` LuxTTS integration into Unreal Engine.
- `#54` Procedural TTS generation route.

## Open issue themes (active planning areas)

- Advanced interaction behavior (`#55`).
- STT overhaul and microphone integration (`#47`).
- Plugin metadata/documentation upkeep (`#46`, `#44`).
- Parsing reliability and command detection (`#40`, `#27`, `#19`).
- TTS plugin and deployment paths (`#39`, `#29`).
- Quest/STT binary compatibility investigation (`#35`).

## Board-level process observations

- Work was tracked as issue clusters by subsystem (STT, parsing, TTS, packaging).
- Multiple updates in the development plan explicitly reference issue IDs and board maintenance.
- Packaging and deployment defects repeatedly fed back into planning priorities, especially for plugin content/binary availability.
