#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "SemanticParser.h"

namespace
{
	void AddAliasRow(UDataTable* Table, const FName RowName, const ENPCAnimationID CommandID, const TArray<FString>& Aliases)
	{
		check(Table);

		FCommandAliasRow Row;
		Row.CommandID = CommandID;
		Row.Aliases = Aliases;
		Table->AddRow(RowName, Row);
	}

	UDataTable* BuildParserTestTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FCommandAliasRow::StaticStruct();

		AddAliasRow(Table, TEXT("Ground"), ENPCAnimationID::ACTION_GROUND, {
			TEXT("get down"),
			TEXT("down"),
			TEXT("on the ground")
		});

		AddAliasRow(Table, TEXT("HandsUp"), ENPCAnimationID::ACTION_HANDS_UP, {
			TEXT("hands up"),
			TEXT("hands in the air"),
			TEXT("put your hands up")
		});

		AddAliasRow(Table, TEXT("HandsBack"), ENPCAnimationID::ACTION_HANDS_BACK, {
			TEXT("hands back"),
			TEXT("behind your back"),
			TEXT("put your hands behind your back")
		});

		return Table;
	}

	void AddTestCaseRow(UDataTable* Table, const FName RowName, const FString& InputText, ENPCAnimationID ExpectedCommand, bool bShouldMatch)
	{
		check(Table);

		FSemanticParseCaseRow Row;
		Row.InputText = InputText;
		Row.ExpectedCommand = ExpectedCommand;
		Row.bShouldMatch = bShouldMatch;
		Table->AddRow(RowName, Row);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSemanticParserCalibrationTest,
	"ParsingSystemUEAddon.SemanticParser.CalibrationCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FSemanticParserCalibrationTest::RunTest(const FString& Parameters)
{
	USemanticParser* Parser = NewObject<USemanticParser>(GetTransientPackage());
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

	UDataTable* Table = BuildParserTestTable();
	TestNotNull(TEXT("Evaluation data table should be created"), Table);
	if (!Table)
	{
		return false;
	}

	Parser->CacheEmbeddingsFromDataTable(Table);

	UDataTable* CasesTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
	CasesTable->RowStruct = FSemanticParseCaseRow::StaticStruct();

	AddTestCaseRow(CasesTable, TEXT("Case_Ground_GetDown"), TEXT("get down"), ENPCAnimationID::ACTION_GROUND, true);
	AddTestCaseRow(CasesTable, TEXT("Case_Ground_Down"), TEXT("down"), ENPCAnimationID::ACTION_GROUND, true);
	AddTestCaseRow(CasesTable, TEXT("Case_Ground_ExtraWords"), TEXT("please go down now"), ENPCAnimationID::ACTION_GROUND, true);
	AddTestCaseRow(CasesTable, TEXT("Case_Ground_FalsePositive"), TEXT("move the chair"), ENPCAnimationID::ACTION_GROUND, false);

	AddTestCaseRow(CasesTable, TEXT("Case_HandsUp_Exact"), TEXT("hands up"), ENPCAnimationID::ACTION_HANDS_UP, true);
	AddTestCaseRow(CasesTable, TEXT("Case_HandsUp_Synonym"), TEXT("hands in the air"), ENPCAnimationID::ACTION_HANDS_UP, true);
	AddTestCaseRow(CasesTable, TEXT("Case_HandsUp_NameNoise"), TEXT("Peter, hands up."), ENPCAnimationID::ACTION_HANDS_UP, true);
	AddTestCaseRow(CasesTable, TEXT("Case_HandsUp_FalsePositive"), TEXT("pen in the air"), ENPCAnimationID::ACTION_NONE, false);

	AddTestCaseRow(CasesTable, TEXT("Case_HandsBack_Exact"), TEXT("hands back"), ENPCAnimationID::ACTION_HANDS_BACK, true);
	AddTestCaseRow(CasesTable, TEXT("Case_HandsBack_Synonym"), TEXT("put your hands behind your back"), ENPCAnimationID::ACTION_HANDS_BACK, true);
	AddTestCaseRow(CasesTable, TEXT("Case_HandsBack_Partial"), TEXT("behind your back"), ENPCAnimationID::ACTION_HANDS_BACK, true);
	AddTestCaseRow(CasesTable, TEXT("Case_HandsBack_FalsePositive"), TEXT("hands in the air again"), ENPCAnimationID::ACTION_NONE, false);

	const FSemanticParseEvaluationReport Report = Parser->EvaluateParsingCasesFromDataTable(CasesTable, 0.65f, 0.08f);

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
		AddInfo(FString::Printf(TEXT("Case='%s' Best='%s' Command=%s Score=%0.4f RunnerUp=%0.4f Margin=%0.4f"),
			*CaseReport.InputText,
			*CaseReport.BestAlias,
			*UEnum::GetValueAsString(CaseReport.BestCommand),
			CaseReport.BestScore,
			CaseReport.RunnerUpScore,
			CaseReport.Margin));
	}

	TestEqual(TEXT("All semantic parser calibration cases should pass"), Report.PassedCases, Report.TotalCases);
	TestEqual(TEXT("No false positives should remain in the calibration suite"), Report.FalsePositives, 0);
	TestEqual(TEXT("No false negatives should remain in the calibration suite"), Report.FalseNegatives, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

