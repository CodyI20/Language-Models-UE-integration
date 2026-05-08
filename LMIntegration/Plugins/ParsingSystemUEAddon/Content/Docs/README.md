# ParsingSystemUEAddon

`ParsingSystemUEAddon` is an Unreal Engine runtime plugin for semantic command parsing.
It takes player input text, compares it against a set of known, preset command aliases, and returns the most likely command as an `ENPCAnimationID`.
The plugin is designed for NPC command interpretation and dialogue output workflows where players can type or speak natural language such as:

- `get down`
- `hands up`
- `put your hands behind your back`

Internally, the system combines:

- an **ONNX reranker / sentence embedding model** running on the CPU via Unreal's NNE framework
- a **tokenizer JSON** used to convert text into token IDs
- **cosine similarity** over cached alias embeddings
- a **lexical F1 overlap score** to automatically balance precision and recall, reducing false positives

The plugin ships as both code and content, so it can be used directly in-game and also showcased through the included demo and automation test assets.

---

## What the plugin does

At a high level, the parser:

1. Loads the ONNX model assigned in the Project Settings.
2. Loads the tokenizer from the plugin's content directory.
3. Reads a `UDataTable` of command aliases.
4. Converts each alias into an embedding and caches it.
5. Accepts raw player input text.
6. Produces the best matching command, or `ACTION_NONE` when confidence is too low or margins are too narrow.

The main runtime entry points live in `USemanticParser`, which is a `UGameInstanceSubsystem`. There is also an async Blueprint node, `UAsyncParseCommand`, for non-blocking use from Blueprint graphs.

---

## Setup & Configuration

### 1) Enable the required plugins
Ensure that Unreal's Neural Network Engine (NNE) plugins are enabled in the project, specifically the CPU runtimes (e.g., `NNERuntimeORT`), as the parser requests the `NNERuntimeORTCpu` runtime on initialization.

### 2) Project Settings
The plugin relies on developer settings to locate its core assets.
Navigate to **Project Settings → Plugins → Semantic Parser** and configure the following:
- **Default Model**: The ONNX model asset (e.g., `all-MiniLM-L6-v2-onnx`).
- **Test Command Aliases Table**: A `UDataTable` using the `FCommandAliasRow` struct.
- **Test Evaluation Cases Table**: A `UDataTable` using the `FSemanticParseCaseRow` struct.

### 3) Initialize the parser before use
`USemanticParser` is a `UGameInstanceSubsystem`, so it is automatically instantiated when the game instance starts. The subsystem will automatically attempt to load the tokenizer and the model from the` Project Settings.

*Note: If you are building tests or editor tools that run outside the normal game instance lifecycle, you can manually call `InitializeForTesting()`.*

### 4) Cache the command aliases
Before you ask the parser to classify input, you must call:
`CacheEmbeddingsFromDataTable(CommandTable)`

This step precomputes the embeddings for every alias and builds the internal lookup caches. Without it, `GetBestMatchingCommand(...)` will not have anything to compare against.

---

## How it scores input

The parser evaluates player input against cached aliases using a hybrid scoring mechanism. Instead of relying on manual tweakable weight arrays, it uses an industry-standard 70/30 split:

1. **Semantic Score (70%)**: Raw cosine similarity between the player's input embedding and the alias embedding. The system may also automatically leverage a "Focused" (noise-filtered) embedding if the player inputs a lot of irrelevant words.
2. **Lexical F1 Score (30%)**: A self-balancing token overlap calculation.
    - **Precision**: How much of the *player's input* was useful? (Punishes extra chatter).
    - **Recall**: How much of the *alias* did the player successfully guess? (Punishes missing keywords).
    - **F1**: The harmonic mean of Precision and Recall.

The final score is clamped between `-1.0` and `1.0`.

### Runtime Thresholds

When querying the parser, you control two primary thresholds to filter out bad matches:

#### `ConfidenceThreshold` (default: `0.70f`)
The minimum hybrid score required for a command to be accepted.
- Lower values accept more fuzzy input.
- Higher values strictly reject borderline phrases.

#### `MinimumMargin` (default: `0.05f` - `0.08f`)
The minimum scoring gap required between the 1st place match and the 2nd place match.
- Lower values allow closer calls to pass.
- Higher values act as an anti-ambiguity guard. If two commands sound similar, a higher margin forces the parser to reject the input rather than guessing the wrong one.

---

## Implementation Guide

### Blueprint usage

1. Get the `USemanticParser` subsystem from the Game Instance.
2. Call `CacheEmbeddingsFromDataTable` using the command table.
3. Use the async node `AsyncGetBestMatchingCommand`.
    - Pass the subsystem, the player's text, the desired `ConfidenceThreshold`, and `MinimumMargin`.
4. Handle the `OnSuccess` or `OnFail` execution pins. `OnSuccess` returns the winning `ENPCAnimationID`.

### C++ usage

```cpp
// 1. Get the subsystem
USemanticParser* SemanticParser = GetGameInstance()->GetSubsystem<USemanticParser>();

// 2. Cache aliases (do this once, e.g., on level load)
SemanticParser->CacheEmbeddingsFromDataTable(MyCommandTable);

// 3. Evaluate text
ENPCAnimationID Result = SemanticParser->GetBestMatchingCommand(PlayerInput, 0.70f, 0.08f);

if (Result != ENPCAnimationID::ACTION_NONE)
{
    // 4. Optionally fetch a dialogue response
    FString ResponseLine = SemanticParser->GetRandomDialogueOption(Result);
    // ... trigger gameplay logic ...
}
```

For debugging or detailed UI feedback, use `GetBestMatchingCommandReport(PlayerInput)`. This returns a struct containing the best alias, the runner-up command, exact scores, and the calculated margin.

---

## Alias and dialogue data

Each row in the command `UDataTable` (`FCommandAliasRow`) contains:

- `CommandID`: The enum value tied to the action (e.g., `ACTION_HANDS_UP`).
- `Aliases`: An array of natural-language phrases players might use. Provide at least 5-10 distinct variations. Focus on realistic phrasing rather than exhaustively listing every possible word combination.
- `DialogueOptions`: (Optional) A list of NPC response lines for the command, retrievable via `GetRandomDialogueOption(...)`.

**Best Practices:**
- Avoid hyper-generic single-word aliases (like "up") if they conflict with other commands.
- Keep aliases focused on intent.
- If you notice false positives, review the aliases first. Broad aliases often trap unintended inputs.

---

## Calibration and testing

The included automated test is the recommended approach for validating the parser configuration:
`ParsingSystemUEAddon.SemanticParser.CalibrationCases`

When run from the Session Frontend or command line, the test will:
1. Initialize a transient parser and load the `TestCommandAliasesTable` and `TestEvaluationCasesTable` configured in Project Settings.
2. Precompute embeddings.
3. Run through every evaluation case to check if it correctly matches or correctly rejects the input.
4. Output a summary containing passed/failed counts, false positives, false negatives, and average scores.

You can override the data tables at test time by passing parameters:
`CommandCsv="Path/To/OverrideCommands.csv" CasesCsv="Path/To/OverrideCases.csv"`

---

## Troubleshooting

- **Parser returns ACTION_NONE constantly:**
  Ensure `CacheEmbeddingsFromDataTable(...)` was actually called and that the tables contain valid `FCommandAliasRow` data. Check the logs to see if the tokenizer or NNE model failed to initialize.

- **Model fails to initialize:**
  Confirm the model is assigned in the Developer Settings (`Project Settings -> Plugins -> Semantic Parser`). Ensure the NNE plugins are enabled.

- **Matches feel too strict:**
  Lower the `ConfidenceThreshold` slightly (e.g., to `0.60`). Ensure the aliases table contains the actual vocabulary you are testing with.

- **Matches feel too permissive (False Positives):**
  Raise the `ConfidenceThreshold` (e.g., to `0.75`). Increase the `MinimumMargin` to force ambiguity rejections. Remove overly generic aliases from the data table.

---

## Notes for maintainers

- `USemanticParser` relies on the `USemanticParserSettings` configuration class. Default assets should be set there rather than hardcoded.
- The tokenizer strictly loads from `Content/NLP_DATA/tokenizer.json` to avoid UAsset packaging complications with raw string data.
- The scoring ensemble logic is centralized in `GetBestMatchingCommandReport`. Any adjustments to lexical or semantic weighting should be done there.
- `EvaluateParsingCasesFromDataTable(...)` leverages the testing tables and is vital for preventing regressions when updating aliases.