# Semantic Parsing System (ONNX + NNE)

## Why this system was introduced

Command interpretation through general-purpose LLM runtime calls added avoidable latency and overhead. The parsing subsystem was introduced to perform fast semantic matching of user text to known command intents. The switch allowed for near-instant execution time.

## Model and runtime strategy

- Sentence-transformer style reranker workflow.
- ONNX model execution through Unreal NNE runtime modules.
- Tokenization through external tokenizer tooling integrated into plugin third-party structure.

## Functional flow

1. Initialize tokenizer and model.
2. Cache command alias embeddings from a data table.
3. Embed input text and compute similarity against cached aliases.
4. Return best match with confidence threshold logic.
5. Use async execution path to avoid main-thread stalls.

## Refactors and robustness improvements

- Command outputs migrated from free-form string handling to enum-based command IDs to reduce typo-driven defects.
- Pluginization was completed and validated in separate project packaging tests.
- Data table-driven command aliasing enabled easier tuning without deep code rewrites.

## Limits and explicit assumptions

- Typo tolerance was not treated as a target requirement for this path because live STT output is the primary input source.
- Hardcoded content/data-table assumptions were identified during migration and flagged for cleanup.
- The alias-matching logic is currently extremely strict, and inputs must closely match the exact command phrasing or known aliases in the data table; otherwise, commands may be missed or mapped to the wrong intent.

## Net result

This subsystem became a key performance-oriented architectural decision: narrower model scope, lower overhead, and cleaner plugin-level reuse compared to generic prompt-response command routing.

## Why the simple string parser was rejected first

The first idea was a plain keyword hashmap: each word in the transcription would be compared against a map of trigger strings (e.g., `"ground"` → `ACTION_ONTHEGROUND`). This was discarded for three assumed reasons:

1. Without a proper implementation, it would fail entirely if the speech-to-text misinterpreted a word.
2. Paraphrases (e.g., "Get down now!" vs "Get on the ground!") would need to be manually added for each possible phrasing, making maintenance unsustainable.
3. It was error-prone and unprofessional; any missing entry silently produced no output.

## Why a sentence-transformer reranker was chosen

A "reranker" model calculates similarity scores between an input sentence and a set of candidate sentences, bypassing the need for literal string equality. The SBERT (sentence-transformers) model family tokenizes input and maps sentences into a dense 384-dimensional vector space. This allows the parser to detect semantic meaning rather than exact word matches.

The model chosen is `all-MiniLM-L6-v2` from Xenova on HuggingFace (a 22.7M parameter model, quantized, originally trained by Microsoft). It maps sentences to a 384-dimensional dense vector space and is optimized for semantic searching and similarity matching. It is many times smaller than even the smallest Ollama models usable in this context (1B+).

## NNE plugin requirements

Three Unreal Engine plugins must be enabled in the project (Edit > Plugins, search "Neural Network Engine"):

- `NNEDenoiser`
- `NNERuntimeCoreML`
- `NNERuntimeORT`

These are in Beta/Experimental state in UE 5.6.1 but operate reliably for this use case.

## Required files from HuggingFace (Xenova/all-MiniLM-L6-v2)

From the "Files and versions" tab on HuggingFace:

- `vocab.txt`
- `tokenizer.json`
- `onnx/model.onnx`

These files must be placed via the filesystem into a folder named `NLP_Data` inside the plugin or project Content folder. Dragging and dropping into the UE Editor will fail due to a compatibility error. Note: The folder will appear empty in the UE Editor even though the files are present.

## Building tokenizers-cpp (third-party dependency)

The all-MiniLM-L6-v2 model requires tokenization before inference. The open-source `tokenizers-cpp` library handles this. Build steps:

1. Clone: `git clone https://github.com/mlc-ai/tokenizers-cpp`
2. Install the Rust toolchain (required for the build)
3. Inside the cloned folder:
   ```
   git submodule update --init --recursive
   mkdir build
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . --config Release
   ```
4. Copy `tokenizers_c.h` and `tokenizers_cpp.h` from the build output into the plugin's `ThirdParty/TokenizersCPP/include` folder
5. Copy `sentencepiece.lib`, `tokenizers_c.lib`, and `tokenizers_cpp.lib` into `ThirdParty/TokenizersCPP/lib/Win64`

## Build.cs configuration

The `ParsingSystemUEAddon.Build.cs` module includes:

- Public dependency: `NNE`
- Include path: `ThirdParty/TokenizersCPP/include`
- Static libraries (Win64 only): `tokenizers_cpp.lib`, `tokenizers_c.lib`, `sentencepiece.lib`
- System libraries required by the Rust runtime: `Bcrypt.lib`, `Userenv.lib`, `ws2_32.lib`, `ntdll.lib`

## C++ class structure

The core C++ class is a `GameInstanceSubsystem` named `SemanticParser`. It:

1. Loads and initializes the tokenizer from the `tokenizer.json` file in `NLP_Data`
2. Loads the ONNX model via `UNNEModelData`
3. Caches embeddings of command aliases from a Data Table (rows of type `CommandAliasRow`, inherited from `FTableRowBase`)
4. Exposes `AsyncGetBestMatchingCommand` which runs on a background thread and calls back with the best-matching `ENPCAnimationID` enum value and a confidence score

## Enum-based command output (refactor)

Before the refactor, command results were FStrings. This caused:

1. Silent bugs from typos in string comparisons
2. Cumbersome codebase as every blueprint and C++ file needed exact matching strings
3. Poor developer experience for anyone new to the project

The `ENPCAnimationID` enum was introduced to hold all supported NPC commands. Each enum element has a human-readable name visible in the UE Editor. Renaming an enum entry propagates automatically through the codebase.

## Data Table setup

A Data Table with row type `CommandAliasRow` is created inside the project. Each row contains:

- A command ID (maps to the `ENPCAnimationID` enum)
- One or more alias strings (the natural-language phrases that should match this command)

The system caches the embeddings of these aliases at startup via `Cache Command Embeddings from Data Table`. In Blueprints, the Data Table is assigned by dragging it from the Content Browser onto the `CommandTable` pin.

## Blueprint integration guide

On `BeginPlay` of the character blueprint:

1. Call `Initialize Tokenizer`
2. Call `Initialize Model` (with the `all-MiniLM-L6-v2-onnx` asset selected for `In Model Data`)
3. Call `Cache Command Embeddings from Data Table` (with the Data Table assigned)

To trigger parsing:

1. Connect the Whisper transcription output (or a text input widget for testing) to `Async Get Best Matching Command`
2. Set the Semantic Parser subsystem reference and a confidence threshold
3. Connect the returned enum value to the animation and/or dialogue system

## Plugin migration

The parsing system was migrated from the main project into the `ParsingSystemUEAddon` plugin:

1. All C++ code moved into the plugin
2. All ThirdParty files (`.lib` and `.h` from tokenizers-cpp) included
3. All UE assets (Data Table, ONNX model) bundled inside plugin Content
4. `.cs` and `.uplugin` updated to declare NNE dependencies

After completion, the plugin was tested in a separate blank C++ UE project and confirmed it was fully plug-and-play. Removing it from the main project's `PublicDependencyModuleNames` no longer caused build errors.

## Packaging fix for parsing system

In packaged builds, the `NLP_Data` folder inside the plugin Content was not reachable. This caused the embedding cache to be empty, making the entire system fail silently. The fix was:
1. Inside the .Build.cs files I added the contents of the "Content" folders to the runtime dependencies as NonUFS staged file type. <img width="643" height="48" alt="image" src="https://github.com/user-attachments/assets/03b8b184-06cc-4da4-a5a0-50a7edb8b418" />

3. Headed to the packaging settings inside the Project Settings tab and added the "Content" folder of each plugin to the "DirectoriesToAlwaysCook" list. <img width="473" height="36" alt="image" src="https://github.com/user-attachments/assets/27305d84-bb05-47ca-af44-83da3dd61c45" />

