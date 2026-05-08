# Semantic Parser Calibration Guide

This guide explains the calibration and export workflow for `USemanticParser`.

## Quick start

1. Create a `UDataTable` asset using `FSemanticParseCaseRow` as the row struct.
2. Fill it with labeled calibration phrases.
3. Call `EvaluateParsingCasesFromDataTable(...)` on the `USemanticParser` instance.
4. Export the result with either:
   - `ExportEvaluationReportToJson(...)`
   - `ExportEvaluationReportToCsv(...)`
5. Inspect false positives and false negatives, then tune the threshold or scoring preset.

## Recommended workflow

### 1) Build a small calibration table
Start with a few positive and negative examples for each command

### 2) Run the evaluation
Call: `EvaluateParsingCasesFromDataTable(EvaluationTable, ConfidenceThreshold, MinimumMargin)`

Suggested starting values:

- `ConfidenceThreshold = 0.65`
- `MinimumMargin = 0.08`

### 3) Review the metrics
Most useful tuning signals:

- **False positives**: non-command phrases being accepted
- **False negatives**: valid commands being rejected
- **Margin**: how clearly the best alias beats the runner-up

### 4) Export the report
Use JSON when you want structured inspection, CSV when you want spreadsheet filtering.

### 5) Tune and repeat
If a phrase like `pen in the air` is incorrectly matching `hands up`, add it as a negative case and rerun the suite until it is rejected.

## Example automation test

The included automation test lives at:

`Plugins/ParsingSystemUEAddon/Source/ParsingSystemUEAddon/Private/Tests/SemanticParserAutomationTests.cpp`

It loads test data from external sources:

- **Command aliases**: DataTable asset `/ParsingSystemUEAddon/DT_SemanticCommands.DT_SemanticCommands`
- **Evaluation cases**: CSV `Plugins/ParsingSystemUEAddon/Content/NLP_Data/SemanticParseCasesTemplate.csv`
  - If the CSV is missing, it falls back to DataTable asset `/ParsingSystemUEAddon/DT_SemanticParserTestCases.DT_SemanticParserTestCases`

Then it runs the evaluation and writes:

- `Saved/SemanticParserCalibration/evaluation-report.json`
- `Saved/SemanticParserCalibration/evaluation-report.csv`

### Running from the automation window

1. Open **Tools → Session Frontend → Automation** in Unreal Editor.
2. Search for `ParsingSystemUEAddon.SemanticParser.CalibrationCases`.
3. Select it and run the test.
4. Open the exported files in `Saved/SemanticParserCalibration/`.

### Optional overrides for custom CSV files

You can pass parser test parameters in the automation command to override data files:

- `CasesCsv=<absolute path to evaluation CSV>`
- `CommandCsv=<absolute path to command alias CSV or DataTable-compatible CSV>`

## Troubleshooting

Here are some issues that you might encounter and how to fix them.

### 1) The parser returns no matches
Check that:
- The tokenizer file exists in the plugin content folder
- The ONNX model loads successfully
- Your alias cache was filled with `CacheEmbeddingsFromDataTable(...)`

### 2) Good commands are still rejected
Lower either: `ConfidenceThreshold` or `MinimumMargin` OR add more representative positive phrases to the calibration table.

### 3) Similar phrases are causing false positives
Add more negative examples and check the margin column in the CSV export.
If the best score is too close to the runner-up, increase the minimum margin slightly. But be careful as it affects the entire evaluation.

### 4) Need stronger single-word matching
Run the aggressive preset. This increases lexical containment boosts meaning that single-word matches are more likely to be accepted, but it can also increase false positives so use it in conjunction with a well-tuned calibration table.

