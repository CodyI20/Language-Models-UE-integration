# Project Scope and Architecture

## Objective

Build a fully local Unreal Engine pipeline where user's speech is transcribed, interpreted, and translated into NPC behavior/dialogue with optional text-to-speech playback.

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
