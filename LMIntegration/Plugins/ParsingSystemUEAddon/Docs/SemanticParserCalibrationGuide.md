# Semantic Parser Calibration Guide

This guide explains the new calibration and export workflow for `USemanticParser`.

## What was added

- DataTable-driven evaluation cases
- Per-case score reporting:
  - best alias
  - best score
  - runner-up score
  - margin
- JSON export of the full report
- CSV export of the full report
- An automation test that exercises the pipeline end to end

## Quick start

1. Create a `UDataTable` asset using `FSemanticParseCaseRow` as the row struct.
2. Fill it with labeled calibration phrases.
3. Call `EvaluateParsingCasesFromDataTable(...)` on your `USemanticParser` instance.
4. Export the result with either:
   - `ExportEvaluationReportToJson(...)`
   - `ExportEvaluationReportToCsv(...)`
5. Inspect false positives and false negatives, then tune the threshold or scoring preset.

## DataTable format

Use these columns:

- `Name` — row name
- `InputText` — phrase to test
- `ExpectedCommand` — enum value such as `ACTION_HANDS_UP`
- `bShouldMatch` — `true` for a positive example, `false` for a negative example

## Recommended workflow

### 1. Build a small calibration table
Start with a few positive and negative examples for each command:

- `ACTION_GROUND`
  - `get down`
  - `down`
  - `please go down now`
  - negative: `move the chair`
- `ACTION_HANDS_UP`
  - `hands up`
  - `hands in the air`
  - `Peter, hands up.`
  - negative: `pen in the air`
- `ACTION_HANDS_BACK`
  - `hands back`
  - `behind your back`
  - `put your hands behind your back`
  - negative: `hands in the air again`

### 2. Run the evaluation
Call:

- `EvaluateParsingCasesFromDataTable(EvaluationTable, ConfidenceThreshold, MinimumMargin)`

Suggested starting values:

- `ConfidenceThreshold = 0.65`
- `MinimumMargin = 0.08`

### 3. Review the metrics
The evaluation report includes:

- `PassedCases`
- `FailedCases`
- `CorrectMatches`
- `CorrectRejections`
- `FalsePositives`
- `FalseNegatives`
- `AverageBestScore`

Most useful tuning signals:

- **False positives**: non-command phrases being accepted
- **False negatives**: valid commands being rejected
- **Margin**: how clearly the best alias beats the runner-up

### 4. Export the report
Use JSON when you want structured inspection, CSV when you want spreadsheet filtering.

### 5. Tune and repeat
If a phrase like `pen in the air` is incorrectly matching `hands up`, add it as a negative case and rerun the suite until it is rejected.

## Example automation test

The included automation test lives at:

`Plugins/ParsingSystemUEAddon/Source/ParsingSystemUEAddon/Private/Tests/SemanticParserAutomationTests.cpp`

It builds a transient evaluation table, runs the evaluation, and writes:

- `Saved/SemanticParserCalibration/evaluation-report.json`
- `Saved/SemanticParserCalibration/evaluation-report.csv`

## Troubleshooting

### The parser returns no matches
Check that:

- the tokenizer file exists in the plugin content folder
- the ONNX model loads successfully
- your alias cache was filled with `CacheEmbeddingsFromDataTable(...)`

### Good commands are still rejected
Lower either:

- `ConfidenceThreshold`
- `MinimumMargin`

or add more representative positive phrases to the calibration table.

### Similar phrases are causing false positives
Add more negative examples and check the margin column in the CSV export.
If the best score is too close to the runner-up, increase the minimum margin slightly.

### Need stronger single-word matching
Run with the aggressive preset flag:

```powershell
-ParsingAggressive
```

This increases lexical containment boosts.

