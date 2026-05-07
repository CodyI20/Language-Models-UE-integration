#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "SemanticParser.h"

namespace
{
	constexpr const TCHAR* CommandAliasesAssetPath = TEXT("/ParsingSystemUEAddon/DT_SemanticCommands.DT_SemanticCommands");
	constexpr const TCHAR* EvaluationCasesAssetPath = TEXT("/ParsingSystemUEAddon/DT_SemanticParserTestCases.DT_SemanticParserTestCases");

	FString GetPluginContentDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ParsingSystemUEAddon"));
		return Plugin.IsValid() ? Plugin->GetContentDir() : FString();
	}

	bool ValidateTableRows(FAutomationTestBase& Test, UDataTable* Table, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel)
	{
		if (!Table)
		{
			Test.AddError(FString::Printf(TEXT("%s table is null."), *SourceLabel));
			return false;
		}

		if (Table->GetRowStruct() != ExpectedRowStruct)
		{
			Test.AddError(FString::Printf(
				TEXT("%s table has incorrect row struct. Expected '%s', got '%s'."),
				*SourceLabel,
				*ExpectedRowStruct->GetName(),
				Table->GetRowStruct() ? *Table->GetRowStruct()->GetName() : TEXT("None")
			));
			return false;
		}

		if (Table->GetRowNames().IsEmpty())
		{
			Test.AddError(FString::Printf(TEXT("%s table has no rows."), *SourceLabel));
			return false;
		}

		return true;
	}

	UDataTable* LoadDataTableFromAsset(FAutomationTestBase& Test, const TCHAR* AssetPath, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel)
	{
		UDataTable* Table = LoadObject<UDataTable>(nullptr, AssetPath);
		if (!Table)
		{
			Test.AddError(FString::Printf(TEXT("Failed to load %s DataTable asset at '%s'."), *SourceLabel, AssetPath));
			return nullptr;
		}

		return ValidateTableRows(Test, Table, ExpectedRowStruct, SourceLabel) ? Table : nullptr;
	}

	UDataTable* LoadDataTableFromCsv(FAutomationTestBase& Test, const FString& CsvFilePath, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel)
	{
		FString CsvText;
		if (!FFileHelper::LoadFileToString(CsvText, *CsvFilePath))
		{
			Test.AddError(FString::Printf(TEXT("Failed to load %s CSV from '%s'."), *SourceLabel, *CsvFilePath));
			return nullptr;
		}

		UDataTable* CsvTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		CsvTable->RowStruct = const_cast<UScriptStruct*>(ExpectedRowStruct);

		const TArray<FString> ImportProblems = CsvTable->CreateTableFromCSVString(CsvText);
		if (!ImportProblems.IsEmpty())
		{
			for (const FString& Problem : ImportProblems)
			{
				Test.AddError(FString::Printf(TEXT("%s CSV parse issue: %s"), *SourceLabel, *Problem));
			}
			return nullptr;
		}

		return ValidateTableRows(Test, CsvTable, ExpectedRowStruct, SourceLabel) ? CsvTable : nullptr;
	}

	UDataTable* LoadDataTableWithCsvOverride(
		FAutomationTestBase& Test,
		const FString& Parameters,
		const TCHAR* ParamKey,
		const TCHAR* AssetPath,
		const UScriptStruct* ExpectedRowStruct,
		const FString& SourceLabel,
		const TCHAR* DefaultCsvName = nullptr)
	{
		FString CsvOverridePath;

		if (FParse::Value(*Parameters, ParamKey, CsvOverridePath))
		{
			Test.AddInfo(FString::Printf(TEXT("Using %s CSV override: %s"), *SourceLabel, *CsvOverridePath));
			return LoadDataTableFromCsv(Test, CsvOverridePath, ExpectedRowStruct, SourceLabel);
		}

		if (DefaultCsvName)
		{
			const FString PluginContentDir = GetPluginContentDir();
			if (!PluginContentDir.IsEmpty())
			{
				const FString DefaultCsvPath = FPaths::Combine(PluginContentDir, TEXT("NLP_Data"), DefaultCsvName);
				if (IFileManager::Get().FileExists(*DefaultCsvPath))
				{
					Test.AddInfo(FString::Printf(TEXT("Using %s CSV: %s"), *SourceLabel, *DefaultCsvPath));
					if (UDataTable* CsvTable = LoadDataTableFromCsv(Test, DefaultCsvPath, ExpectedRowStruct, SourceLabel))
					{
						return CsvTable;
					}
				}
			}
		}

		Test.AddInfo(FString::Printf(TEXT("Using %s DataTable asset: %s"), *SourceLabel, AssetPath));
		return LoadDataTableFromAsset(Test, AssetPath, ExpectedRowStruct, SourceLabel);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSemanticParserCalibrationTest,
	"ParsingSystemUEAddon.SemanticParser.CalibrationCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FSemanticParserCalibrationTest::RunTest(const FString& Parameters)
{
	UGameInstance* TestGameInstance = NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Test game instance should be created"), TestGameInstance);
	if (!TestGameInstance)
	{
		return false;
	}

	USemanticParser* Parser = NewObject<USemanticParser>(TestGameInstance);
	TestNotNull(TEXT("Parser object should be created"), Parser);
	if (!Parser)
	{
		return false;
	}

	if (!Parser->InitializeForTesting())
	{
		AddError(TEXT("Failed to initialize tokenizer/model for the semantic parser test harness."));
		return false;
	}

	UDataTable* CommandTable = LoadDataTableWithCsvOverride(
		*this,
		Parameters,
		TEXT("CommandCsv="),
		CommandAliasesAssetPath,
		FCommandAliasRow::StaticStruct(),
		TEXT("Command aliases"));
	TestNotNull(TEXT("Command aliases table should be loaded"), CommandTable);
	if (!CommandTable)
	{
		return false;
	}

	Parser->CacheEmbeddingsFromDataTable(CommandTable);

	UDataTable* CasesTable = LoadDataTableWithCsvOverride(
		*this,
		Parameters,
		TEXT("CasesCsv="),
		EvaluationCasesAssetPath,
		FSemanticParseCaseRow::StaticStruct(),
		TEXT("Evaluation cases"));
	TestNotNull(TEXT("Evaluation cases table should be loaded"), CasesTable);
	if (!CasesTable)
	{
		return false;
	}

	const FSemanticParseEvaluationReport Report = Parser->EvaluateParsingCasesFromDataTable(CasesTable, Report.CaseReports[0].ConfidenceThreshold, Report.CaseReports[0].MinimumMargin);

	AddInfo(FString::Printf(TEXT("Semantic parser evaluation: %d/%d passed, avg best score=%0.4f, false positives=%d, false negatives=%d"),
		Report.PassedCases,
		Report.TotalCases,
		Report.AverageBestScore,
		Report.FalsePositives,
		Report.FalseNegatives));

	const FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SemanticParserCalibration"));
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	const FString JsonPath = FPaths::Combine(OutputDir, TEXT("evaluation-report.json"));
	const FString CsvPath = FPaths::Combine(OutputDir, TEXT("evaluation-report.csv"));
	const FString JsonText = Parser->ExportEvaluationReportToJson(Report, JsonPath, true);
	const FString CsvText = Parser->ExportEvaluationReportToCsv(Report, CsvPath);

	TestTrue(TEXT("JSON export should not be empty"), !JsonText.IsEmpty());
	TestTrue(TEXT("CSV export should not be empty"), !CsvText.IsEmpty());
	TestTrue(TEXT("JSON export file should exist"), IFileManager::Get().FileExists(*JsonPath));
	TestTrue(TEXT("CSV export file should exist"), IFileManager::Get().FileExists(*CsvPath));

	for (const FSemanticParseScoreReport& CaseReport : Report.CaseReports)
	{
		const bool bAccepted =
			(CaseReport.BestCommand != ENPCAnimationID::ACTION_NONE) &&
			(CaseReport.BestScore >= CaseReport.ConfidenceThreshold) &&
			(CaseReport.Margin >= CaseReport.MinimumMargin);

		if (CaseReport.ExpectedCommand == ENPCAnimationID::ACTION_NONE && bAccepted)
		{
			AddError(FString::Printf(
				TEXT("Expected ACTION_NONE case was accepted by thresholds: Input='%s'; Actual=%s; Score=%0.4f; Margin=%0.4f"),
				*CaseReport.InputText,
				*UEnum::GetValueAsString(CaseReport.BestCommand),
				CaseReport.BestScore,
				CaseReport.Margin));
		}

		if (!CaseReport.bPassed)
		{
			AddError(FString::Printf(
				TEXT("FAILED CASE: Input='%s'; Expected=%s; Actual=%s; BestAlias='%s'; BestScore=%0.4f; RunnerUpCommand=%s; RunnerUpScore=%0.4f; Margin=%0.4f; PresetUsed=%s"),
				*CaseReport.InputText,
				*UEnum::GetValueAsString(CaseReport.ExpectedCommand),
				*UEnum::GetValueAsString(CaseReport.BestCommand),
				*CaseReport.BestAlias,
				CaseReport.BestScore,
				*UEnum::GetValueAsString(CaseReport.RunnerUpCommand),
				CaseReport.RunnerUpScore,
				CaseReport.Margin,
				*UEnum::GetValueAsString(CaseReport.ScorePreset)));
		}else
		{
			AddInfo(FString::Printf(TEXT("SUCCESSFUL CASE: Input='%s'; Passed Command=%s; Score=%0.4f; RunnerUp=%0.4f; Margin=%0.4f; PresetUsed=%s"),
			*CaseReport.InputText,
			*UEnum::GetValueAsString(CaseReport.BestCommand),
			CaseReport.BestScore,
			CaseReport.RunnerUpScore,
			CaseReport.Margin,
			*UEnum::GetValueAsString(CaseReport.ScorePreset)));
		}
	}

	TestEqual(TEXT("All semantic parser calibration cases should pass"), Report.PassedCases, Report.TotalCases);
	TestEqual(TEXT("No false positives should remain in the calibration suite"), Report.FalsePositives, 0);
	TestEqual(TEXT("No false negatives should remain in the calibration suite"), Report.FalseNegatives, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS