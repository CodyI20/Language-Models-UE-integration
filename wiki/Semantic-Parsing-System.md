# Semantic Parsing System (ONNX + NNE)

## Why this system was introduced

Command interpretation through general-purpose LLM runtime calls added avoidable latency and overhead. The parsing subsystem was introduced to perform fast semantic matching of user text to known command intents.

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

## Net result

This subsystem became a key performance-oriented architectural decision: narrower model scope, lower overhead, and cleaner plugin-level reuse compared to generic prompt-response command routing.
