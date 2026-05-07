# ParsingSystemUEAddon

`ParsingSystemUEAddon` is an Unreal Engine runtime plugin for semantic command parsing.
It takes player input text, compares it against a set of known, preset command aliases, and returns the most likely command as an `ENPCAnimationID`.
The plugin is designed for NPC command interpretation and dialogue output workflows where players can type or speak natural language such as:

- `get down`
- `hands up`
- `put your hands behind your back`

Internally, the system combines:

- a **MiniLM ONNX reranker / sentence embedding model** (`all-MiniLM-L6-v2-onnx`)
- a **tokenizer JSON** used to convert text into token IDs
- **cosine similarity** over cached alias embeddings
- extra **lexical overlap boosts / penalties** to reduce false positives

The plugin ships as both code and content, so it can be used directly in-game and also showcased through the included demo and automation test assets.

---

## What the plugin does

At a high level, the parser:

1. Loads the ONNX model from plugin content.
2. Loads the tokenizer from plugin content.
3. Reads a `UDataTable` of command aliases.
4. Converts each alias into an embedding and caches it.
5. Accepts raw player input text.
6. Produces the best matching command, or `ACTION_NONE` when confidence is too low.

The main runtime entry points live in `USemanticParser`, which is a `UGameInstanceSubsystem`. There is also an async Blueprint node, `UAsyncParseCommand`, for non-blocking use from Blueprint graphs.

---

## Where the important content lives

Plugin content and points of interest in the code:

- `Plugins/ParsingSystemUEAddon/Content/LMModel/`
  - ONNX model asset used by the parser
- `Plugins/ParsingSystemUEAddon/Content/NLP_Data/tokenizer.json`
  - tokenizer blob loaded at runtime
- `Plugins/ParsingSystemUEAddon/Content/DT_SemanticCommands.uasset`
  - command alias table
- `Plugins/ParsingSystemUEAddon/Content/DT_SemanticParserTestCases.uasset`
  - calibration / test-case table for automated testing
- `Plugins/ParsingSystemUEAddon/Content/NLP_Data/SemanticParseCasesTemplate.csv`
  - example CSV template for evaluation cases
- `Plugins/ParsingSystemUEAddon/Content/Demo/DemoLevel.umap`
  - demo map
- `Plugins/ParsingSystemUEAddon/Content/SemanticParserComponent.uasset`
  - default component blueprint
- `Plugins/ParsingSystemUEAddon/Content/NPC/`
  - default NPC-related assets
- `Plugins/ParsingSystemUEAddon/Content/Docs/SemanticParserCalibrationGuide.md`
  - calibration and export workflow guide

---

## Setup

### 1) Enable the plugin dependencies

`ParsingSystemUEAddon.uplugin` already enables these plugins:

- `NNEDenoiser` (UE-native)
- `NNERuntimeCoreML`(UE-native)
- `NNERuntimeORT`(UE-native)
- `UTLogger`(External plugin - REQUIRED; No backwards compatibility with the UE_LOG reflection macro to fall back to)

The parser uses the **ORT CPU runtime** at startup, so the runtime dependency that matters most is `NNERuntimeORT`.

### 2) Make sure the content is cooked

The project packaging config already stages the plugin content with:

- `/NNEDenoiser`
- `/ParsingSystemUEAddon`
- `/SpeechToText`

This is important because the parser loads assets by plugin path at runtime, including:

- `/ParsingSystemUEAddon/LMModel/all-MiniLM-L6-v2-onnx.all-MiniLM-L6-v2-onnx`
- `/ParsingSystemUEAddon/NLP_DATA/tokenizer.json`

If those assets are to be renamed or moved, ensure the hardcoded paths in the parser code are updated.

### 3) Verify the command alias table

The parser needs a `UDataTable` built from `FCommandAliasRow`.
The included command table asset is:

- `/ParsingSystemUEAddon/DT_SemanticCommands.DT_SemanticCommands`

It can be used as-is, upgraded/improved, or just used as a template for a custom command set.

### 4) Initialize the parser before use

`USemanticParser` is a `UGameInstanceSubsystem`, so it is normally initialized automatically when the game instance starts.

*IMPORTANT: For tests or editor tools, call: `InitializeForTesting()`. It loads both the tokenizer and the model without depending on the normal subsystem lifecycle.*

### 5) Cache the command aliases

Before you ask the parser to classify input, call:
`CacheEmbeddingsFromDataTable(CommandTable)`

This step precomputes the embeddings for every alias and builds the internal lookup caches.
Without it, `GetBestMatchingCommand(...)` will not have anything to compare against.

---

## Important variables and what they do

`FSemanticParserTweakableSettings` contains the main tuning values.
These settings affect how aggressively the parser accepts text and how much it rewards lexical overlap.

### Score preset

- Enum: `ScorePreset` (`Safe` or `Aggressive`)

This selects which group of tuning values should be used.

General guidance:

- **Safe**: better when you want to avoid false positives (aka recognizing phrases that are not actually commands, as commands)
- **Aggressive**: better when you want the parser to accept more loosely phrased commands

### Safe preset tuning

These fields are used when `ScorePreset = Safe`:

- `SafeAliasContainedBoost`
  - bonus when the alias tokens are fully contained in the input
- `SafeInputContainedBoost`
  - bonus when the input tokens are fully contained in the alias
- `SafeCoverageBlendWeight`
  - controls how much partial token overlap influences the score
- `SafeMaxLexicalBoost`
  - ceiling for the lexical boost
- `SafeMismatchPenaltyWeight`
  - penalty strength when alias and input do not align cleanly
- `SafeNoOverlapPenalty`
  - penalty applied when there is no token overlap at all
- `SafeMaxLexicalPenalty`
  - maximum penalty allowed from lexical mismatch
- `SafeAmbiguousOverlapPenalty`
  - penalty when overlap is incomplete and potentially ambiguous
- `SafeMinAliasCoverageForFocusedBoost`
  - minimum alias coverage needed before the focused-input path is considered strong
- `SafeFocusedBlendWeight`
  - how much the focused embedding can influence the final score
- `SafeFocusedFallbackBlendWeight`
  - fallback blend weight when the focused embedding does not strongly outperform the raw embedding

### Aggressive preset tuning

The fields which start with `Aggresive` are used when `ScorePreset = Aggressive`:

In practice, the aggressive values make the parser more willing to accept fuzzy or partial matches.
That can improve convenience, but it can also increase false positives if the aliases are too broad.

### Runtime thresholds

The most important runtime thresholds are:

- `ConfidenceThreshold`
- `MinimumMargin`

They are used by `GetBestMatchingCommand(...)` and the evaluation functions.

#### `ConfidenceThreshold` (default: `0.65f`)

This is the minimum similarity score required for a command to be accepted.

- Lower values accept more input
- Higher values reject more borderline phrases

If you lower this too much, very weak matches can start getting accepted.
If you raise it too much, valid user phrases may be rejected.

#### `MinimumMargin` (default: `0.10f`)

This is the minimum gap between the best match and the runner-up.

- Lower values allow closer calls to pass
- Higher values require a clearer winner

This is a great anti-ambiguity guard.
If two commands are semantically similar, a low margin may cause the wrong command to be accepted.

### Evaluation report fields

`FSemanticParseEvaluationReport` tracks the overall quality of a test run.
These are the fields to inspect when you are tuning the system.

### Alias and dialogue data

Quick reminder:

Each `FCommandAliasRow` contains:

- `CommandID`: The command enum value, such as `ACTION_GROUND`
- `Aliases`: One or more natural-language phrases for that command. The recommended amount is at least 10, since the more aliases, the more accurate the results are. NOTE: They don't have to match exactly, so DO NOT write all the possible variations.
- `DialogueOptions`: (Optional) response lines for the same command. This pairs greatly with the text-to-speech functionality.

**Important behavior notes:**

- Every alias in `Aliases` is cached separately.
- Each alias should be something a player might actually say.
- `DialogueOptions` are used by `GetRandomDialogueOption(...)`.
- The current implementation expects at least one dialogue option for any command that you plan to sample randomly.

If you add very generic aliases such as `down` or `up`, the parser may match them too easily.
If you add too many near-duplicate aliases across different commands, the margin between commands can shrink and cause ambiguous results.

---

## How to use it in a game or application

### Blueprint usage (example can be found in the demo setup)

1. Get the `USemanticParser` subsystem from the game instance.
2. Cache the command aliases from your `UDataTable`.
3. Send player text to the parser.
4. Handle success or failure.

The async node is the easiest way to do this in Blueprint:

- `AsyncGetBestMatchingCommand(ParserSystem, PlayerInput, ConfidenceThreshold)`

It exposes:

- `OnSuccess`
- `OnFail`

Use `OnSuccess` when the parser returns a command other than `ACTION_NONE`.
Use `OnFail` when parsing fails, the parser is not initialized, or the match does not meet the threshold.

### C++ usage

1. Get the subsystem: `GetGameInstance()->GetSubsystem<USemanticParser>()`
2. Load or reference the command table.
3. Call `CacheEmbeddingsFromDataTable(CommandTable)`.
4. Call `GetBestMatchingCommand(PlayerInput, ConfidenceThreshold, MinimumMargin)`. (or the `Async` version)
5. Switch on the returned `ENPCAnimationID`.

For more detail for debugging or calibration, call: `GetBestMatchingCommandReport(PlayerInput)`.
This function gives the best alias, runner-up, and scoring metadata.

### Example runtime pattern

Cache aliases once at startup → Parse each input string on demand →
Use the result to trigger gameplay logic → Optionally fetch a dialogue response with `GetRandomDialogueOption(CommandID)`

### Recommended integration habits

- Keep command aliases short, representative, and diverse
- Use a `ConfidenceThreshold` that is strict enough for your game
- Add a `MinimumMargin` if you have commands that sound alike
- Re-run the calibration test after changing aliases
- Inspect false positives first; they usually indicate overly broad aliases or too-low thresholds

---

## Demo setup

The plugin includes a small demo content set under `Content/Demo/` and related assets in `Content/NPC/`.

The demo assets show a simple end-to-end loop:
Player input
→ Semantic parsing
→ Command selection
→ NPC reaction (animation + optionally dialogue)

This is great for manually testing the system, but
for calibration or large-scale tests, the included automated test is the recommended approach.

---

## Calibration and testing

The included calibration workflow is documented in:

- `Plugins/ParsingSystemUEAddon/Content/Docs/SemanticParserCalibrationGuide.md`

The automation test is:

- `ParsingSystemUEAddon.SemanticParser.CalibrationCases`

It loads:

- Command aliases from `/ParsingSystemUEAddon/DT_SemanticCommands.DT_SemanticCommands`
- Evaluation cases from `Plugins/ParsingSystemUEAddon/Content/NLP_Data/SemanticParseCasesTemplate.csv`
  - or from `/ParsingSystemUEAddon/DT_SemanticParserTestCases.DT_SemanticParserTestCases` if needed

It then writes reports to:

- `Saved/SemanticParserCalibration/evaluation-report.json`
- `Saved/SemanticParserCalibration/evaluation-report.csv`

This is the best way to validate changes to aliases, thresholds, or tuning values.

---

## Troubleshooting

### Parser returns no result

Check that:

- the plugin is enabled
- the tokenizer exists at `Content/NLP_Data/tokenizer.json`
- the ONNX model asset is present and cookable
- you called `CacheEmbeddingsFromDataTable(...)`
- your `UDataTable` uses `FCommandAliasRow`

### Parser feels too strict

Try one or more of:

- lower `ConfidenceThreshold`
- lower `MinimumMargin`
- use the aggressive preset
- add more aliases that reflect real player phrasing

### Parser feels too permissive

Try one or more of:

- raise `ConfidenceThreshold`
- raise `MinimumMargin`
- use the safe preset
- remove aliases that are too generic
- add negative calibration cases to the evaluation table

### Dialogue selection fails or feels empty

Make sure the chosen command has at least one `DialogueOptions` entry.

### Packaging issues

If the parser works in-editor but fails in a packaged build, verify that:

- the plugin content is being cooked
- the hardcoded asset paths still match the on-disk assets
- the runtime dependencies are enabled in the packaged build

---

## Notes for maintainers

A few implementation details worth remembering when updating this plugin:

- `USemanticParser` is a `UGameInstanceSubsystem`.
- The parser currently loads the tokenizer from `Content/NLP_DATA/tokenizer.json`.
- The model is loaded from the `/ParsingSystemUEAddon/LMModel/...` asset path.
- `EvaluateParsingCasesFromDataTable(...)` is the preferred way to run labeled calibration tables.
- `InitializeVariables()` exists as a helper for applying tuning presets, so if you change the tuning flow, make sure the runtime path still applies the preset values you expect.

When in doubt, compare the README against:

- `Plugins/ParsingSystemUEAddon/Source/ParsingSystemUEAddon/Public/SemanticParser.h`
- `Plugins/ParsingSystemUEAddon/Source/ParsingSystemUEAddon/Private/SemanticParser.cpp`
- `Plugins/ParsingSystemUEAddon/Source/ParsingSystemUEAddon/Public/AsyncParseCommand.h`
- `Plugins/ParsingSystemUEAddon/Source/ParsingSystemUEAddon/Private/Tests/SemanticParserAutomationTests.cpp`

---

## Quick start summary

1. Put your aliases in `DT_SemanticCommands`.
2. Call `CacheEmbeddingsFromDataTable(...)`.
3. Parse player text with `GetBestMatchingCommand(...)` or `AsyncGetBestMatchingCommand(...)`.
4. Tune `ConfidenceThreshold` and `MinimumMargin` until false positives and false negatives look acceptable.
5. Use the demo level if you want a simple example of the full pipeline.

