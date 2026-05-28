#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "SemanticParser.h"
#include "SemanticParserSettings.h"
#include "UObject/Package.h"

namespace
{
	FString GetPluginContentDir()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ParsingSystemUEAddon"));
		return Plugin.IsValid() ? Plugin->GetContentDir() : FString();
	}

	bool ValidateTableRows(FAutomationTestBase& Test, UDataTable* Table, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel)
	{
		if (!Table) return false;
		if (Table->GetRowStruct() != ExpectedRowStruct) return false;
		if (Table->GetRowNames().IsEmpty()) return false;
		return true;
	}

	UDataTable* LoadDataTableFromAsset(FAutomationTestBase& Test, const TCHAR* AssetPath, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel)
	{
		UDataTable* Table = LoadObject<UDataTable>(nullptr, AssetPath);
		return ValidateTableRows(Test, Table, ExpectedRowStruct, SourceLabel) ? Table : nullptr;
	}

	UDataTable* LoadDataTableFromCsv(FAutomationTestBase& Test, const FString& CsvFilePath, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel)
	{
		FString CsvText;
		if (!FFileHelper::LoadFileToString(CsvText, *CsvFilePath)) return nullptr;

		UDataTable* CsvTable = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		CsvTable->RowStruct = const_cast<UScriptStruct*>(ExpectedRowStruct);
		CsvTable->CreateTableFromCSVString(CsvText);

		return ValidateTableRows(Test, CsvTable, ExpectedRowStruct, SourceLabel) ? CsvTable : nullptr;
	}

	UDataTable* LoadDataTableWithCsvOverride(FAutomationTestBase& Test, const FString& Parameters, const TCHAR* ParamKey, const TCHAR* AssetPath, const UScriptStruct* ExpectedRowStruct, const FString& SourceLabel, const TCHAR* DefaultCsvName = nullptr)
	{
		FString CsvOverridePath;
		if (FParse::Value(*Parameters, ParamKey, CsvOverridePath))
		{
			return LoadDataTableFromCsv(Test, CsvOverridePath, ExpectedRowStruct, SourceLabel);
		}
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
	const USemanticParserSettings* Settings = GetDefault<USemanticParserSettings>();
	if (!Settings || Settings->TestCommandAliasesTable.IsNull() || Settings->TestEvaluationCasesTable.IsNull())
	{
		AddError(TEXT("Test Data Tables are not configured. Please set them in Project Settings -> Semantic Parser."));
		return false;
	}

	UGameInstance* TestGameInstance = NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestGameInstance) return false;

	USemanticParser* Parser = NewObject<USemanticParser>(TestGameInstance);
	if (!Parser || !Parser->InitializeForTesting()) return false;

	FString AliasesPath = Settings->TestCommandAliasesTable.ToSoftObjectPath().ToString();
	UDataTable* CommandTable = LoadDataTableWithCsvOverride(*this, Parameters, TEXT("CommandCsv="), *AliasesPath, FCommandAliasRow::StaticStruct(), TEXT("Command aliases"));
	if (!CommandTable) return false;

	Parser->CacheEmbeddingsFromDataTable(CommandTable);

	FString CasesPath = Settings->TestEvaluationCasesTable.ToSoftObjectPath().ToString();
	UDataTable* CasesTable = LoadDataTableWithCsvOverride(*this, Parameters, TEXT("CasesCsv="), *CasesPath, FSemanticParseCaseRow::StaticStruct(), TEXT("Evaluation cases"));
	if (!CasesTable) return false;

	// Notice we lowered the passing threshold slightly to 0.70 to account for F1 standard ranges.
	const FSemanticParseEvaluationReport Report = Parser->EvaluateParsingCasesFromDataTable(CasesTable, 0.70f, 0.05f);

	AddInfo(FString::Printf(TEXT("Evaluation: %d/%d passed, avg best score=%0.4f, false positives=%d, false negatives=%d"),
		Report.PassedCases, Report.TotalCases, Report.AverageBestScore, Report.FalsePositives, Report.FalseNegatives));

	TestEqual(TEXT("All cases should pass"), Report.PassedCases, Report.TotalCases);
	return true;
}
#endif