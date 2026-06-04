// Fill out your copyright notice in the Description page of Project Settings.

#include "SemanticParser.h"
#include "SemanticParserSettings.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "NNE.h"
#include "Core/Log.h"
#include "NNEModelData.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"

THIRD_PARTY_INCLUDES_START
#include "tokenizers_cpp.h"
THIRD_PARTY_INCLUDES_END

using namespace tokenizers;

void USemanticParser::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializeTokenizer();
	InitializeModel();
}

bool USemanticParser::InitializeForTesting()
{
	return InitializeTokenizer() && InitializeModel();
}

bool USemanticParser::InitializeModel()
{
	ULog::Info(TEXT("SemanticParser.cpp - InitializeModel"), TEXT("Initializing Semantic Parser!"));

	const USemanticParserSettings* Settings = GetDefault<USemanticParserSettings>();
	if (!Settings || Settings->DefaultModel.IsNull())
	{
		ULog::Error(TEXT("SemanticParser.cpp - InitializeModel"), TEXT("Model path is not configured in Project Settings -> Semantic Parser."));
		return false;
	}

	UNNEModelData* FoundModel = Settings->DefaultModel.LoadSynchronous();
	if (!FoundModel)
	{
		ULog::Error(TEXT("SemanticParser.cpp - InitializeModel"), TEXT("Failed to load configured model."));
		return false;
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(FString("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		ULog::Error(TEXT("SemanticParser.cpp - InitializeModel"), TEXT("NNE ONNX CPU Runtime is not valid."));
		return false;
	}

	TSharedPtr<UE::NNE::IModelCPU> Model = Runtime->CreateModelCPU(FoundModel);
	if (Model.IsValid())
	{
		ModelInstance = Model->CreateModelInstanceCPU();
		ULog::Info(TEXT("SemanticParser.cpp - InitializeModel"), TEXT("Model loaded successfully!"));;
		return ModelInstance.IsValid();
	}

	return false;
}

bool USemanticParser::InitializeTokenizer()
{
	ULog::Info(TEXT("SemanticParser.cpp - InitializeTokenizer"), TEXT("Initializing the tokenizer"));

	FString TokenizerFilePath = GetTokenizerFilePath();
	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *TokenizerFilePath))
	{
		ULog::Error(TEXT("SemanticParser.cpp - InitializeTokenizer"), *FString::Printf(TEXT("Failed to read the file contents at: %s"), *TokenizerFilePath));
		return false;
	}

	std::string StdJsonBlob = TCHAR_TO_UTF8(*JsonContent);
	if (auto Tokenizer = Tokenizer::FromBlobJSON(StdJsonBlob))
	{
		TokenizerInstance = new std::unique_ptr<class Tokenizer>(std::move(Tokenizer));
		return true;
	}

	ULog::Error(TEXT("SemanticParser.cpp - InitializeTokenizer"), TEXT("Failed to parse Tokenizer JSON data."));
	return false;
}

TArray<float> USemanticParser::GetSemanticEmbedding(const TArray<int64>& InputIDs) const
{
	if (!ModelInstance.IsValid()) return TArray<float>();

	TArray<int64> CleanInputIDs;
	TArray<int64> CleanAttentionMask;

	for (int32 i = 0; i < InputIDs.Num(); ++i)
	{
		if (InputIDs[i] != 0)
		{
			CleanInputIDs.Add(InputIDs[i]);
			CleanAttentionMask.Add(1); 
		}
	}

	if (CleanInputIDs.IsEmpty()) return TArray<float>();

	int32 NumInputs = ModelInstance->GetInputTensorDescs().Num();
	TArray<int64> TokenTypeIDs;
	TokenTypeIDs.Init(0, CleanInputIDs.Num());

	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.SetNumZeroed(NumInputs);

	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.SetNum(NumInputs);

	TConstArrayView<UE::NNE::FTensorDesc> InputDescs = ModelInstance->GetInputTensorDescs();

	for (int32 i = 0; i < NumInputs; ++i)
	{
		FString InputName = InputDescs[i].GetName();
		if (InputName.Contains(TEXT("input_ids")))
		{
			InputBindings[i].Data = static_cast<void*>(CleanInputIDs.GetData());
			InputBindings[i].SizeInBytes = CleanInputIDs.Num() * sizeof(int64);
			InputShapes[i] = UE::NNE::FTensorShape::Make({1, static_cast<uint32>(CleanInputIDs.Num())});
		}
		else if (InputName.Contains(TEXT("attention_mask")))
		{
			InputBindings[i].Data = static_cast<void*>(CleanAttentionMask.GetData());
			InputBindings[i].SizeInBytes = CleanAttentionMask.Num() * sizeof(int64);
			InputShapes[i] = UE::NNE::FTensorShape::Make({1, static_cast<uint32>(CleanAttentionMask.Num())});
		}
		else if (InputName.Contains(TEXT("token_type_ids")))
		{
			InputBindings[i].Data = static_cast<void*>(TokenTypeIDs.GetData());
			InputBindings[i].SizeInBytes = TokenTypeIDs.Num() * sizeof(int64);
			InputShapes[i] = UE::NNE::FTensorShape::Make({1, static_cast<uint32>(TokenTypeIDs.Num())});
		}
	}

	if (ModelInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok) return TArray<float>();

	uint64 TotalOutputFloats = CleanInputIDs.Num() * EmbeddingDimension;
	TArray<float> RawOutput;
	RawOutput.SetNumZeroed(TotalOutputFloats);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.SetNumZeroed(1);
	OutputBindings[0].Data = static_cast<void*>(RawOutput.GetData());
	OutputBindings[0].SizeInBytes = RawOutput.Num() * sizeof(float);

	if (ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
	{
		int32 SeqLen = CleanInputIDs.Num();
		TArray<float> FinalEmbedding;
		FinalEmbedding.SetNumZeroed(EmbeddingDimension);

		int32 ValidTokens = 0;
		if (SeqLen > 2)
		{
			for (int32 TokenIdx = 1; TokenIdx < SeqLen - 1; ++TokenIdx)
			{
				ValidTokens++;
				for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim)
				{
					int32 RawIndex = (TokenIdx * EmbeddingDimension) + Dim;
					if (RawIndex < RawOutput.Num()) FinalEmbedding[Dim] += RawOutput[RawIndex];
				}
			}
		}
		else 
		{
			for (int32 TokenIdx = 0; TokenIdx < SeqLen; ++TokenIdx)
			{
				ValidTokens++;
				for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim)
				{
					int32 RawIndex = (TokenIdx * EmbeddingDimension) + Dim;
					if (RawIndex < RawOutput.Num()) FinalEmbedding[Dim] += RawOutput[RawIndex];
				}
			}
		}

		if (ValidTokens > 0)
		{
			for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim) FinalEmbedding[Dim] /= static_cast<float>(ValidTokens);
		}

		float Magnitude = 0.0f;
		for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim) Magnitude += FinalEmbedding[Dim] * FinalEmbedding[Dim];

		if (Magnitude > 0.0f)
		{
			float SqrtMagnitude = FMath::Sqrt(Magnitude);
			float InvMag = 1.0f / SqrtMagnitude;
			for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim) FinalEmbedding[Dim] *= InvMag;
		}
		return FinalEmbedding;
	}

	return TArray<float>();
}

FSemanticParseScoreReport USemanticParser::GetBestMatchingCommandReport(const FString& PlayerInput) const
{
	FSemanticParseScoreReport Report;
	Report.InputText = PlayerInput;

	if (CachedAliasEmbeddings.IsEmpty())
	{
		ULog::Error(TEXT("SemanticParser.cpp - GetBestMatchingCommand"), TEXT("Cache is empty!"));
		return Report;
	}

	TArray<int64> InputIDs;
	if (!TokenizeString(PlayerInput, InputIDs))
	{
		ULog::Error(TEXT("SemanticParser.cpp - GetBestMatchingCommand"), TEXT("Tokenization Failed"));
		return Report;
	}

	TArray<float> PlayerEmbedding = GetSemanticEmbedding(InputIDs);
	if (PlayerEmbedding.IsEmpty())
	{
		ULog::Error(TEXT("SemanticParser.cpp - GetBestMatchingCommand"), TEXT("Embedding Failed"));
		return Report;
	}

	// Noise filter embedding
	const TArray<int64> FocusedInputIDs = BuildFocusedInputIDs(InputIDs, AliasVocabularyTokenSet);
	TArray<float> FocusedPlayerEmbedding;
	const int32 RawContentTokenCount = CountContentTokens(InputIDs);
	const int32 FocusedContentTokenCount = CountContentTokens(FocusedInputIDs);
	if (FocusedContentTokenCount > 0 && FocusedContentTokenCount < RawContentTokenCount)
	{
		FocusedPlayerEmbedding = GetSemanticEmbedding(FocusedInputIDs);
	}

	const TSet<int64> InputTokenSet = ExtractContentTokenSet(InputIDs);

	ENPCAnimationID WinningCommand = ENPCAnimationID::ACTION_NONE;
	ENPCAnimationID RunnerUpCommand = ENPCAnimationID::ACTION_NONE;
	FString BestAlias = TEXT("None");
	float BestScore = -1.0f;
	float RunnerUpScore = -1.0f;
	TMap<ENPCAnimationID, float> BestScorePerCommand;

	// Hardcoded Industry Standard NLP Ensemble Weights
	const float SemanticWeight = 0.70f;
	const float LexicalWeight = 0.30f;

	for (const auto& CachedPair : CachedAliasEmbeddings)
	{
		const FString& AliasText = CachedPair.Key;
		const TArray<float>& AliasVector = CachedPair.Value;
		const ENPCAnimationID* FoundCommand = AliasToCommandMap.Find(AliasText);
		if (!FoundCommand) continue;

		// Raw semantic score
		float SemanticScore = 0.0f;
		for (int32 i = 0; i < EmbeddingDimension; ++i)
		{
			SemanticScore += PlayerEmbedding[i] * AliasVector[i];
		}
		
		if (!FocusedPlayerEmbedding.IsEmpty())
		{
			float FocusedScore = 0.0f;
			for (int32 i = 0; i < EmbeddingDimension; ++i)
			{
				FocusedScore += FocusedPlayerEmbedding[i] * AliasVector[i];
			}
			// Automatically trust the noise-filtered embedding if it's stronger
			if (FocusedScore > SemanticScore)
			{
				SemanticScore = FMath::Lerp(SemanticScore, FocusedScore, 0.85f);
			}
		}
		
		float LexicalF1Score = 0.0f;
		if (const TSet<int64>* AliasTokenSet = CachedAliasTokenSets.Find(AliasText))
		{
			int32 OverlapCount = 0;
			for (const int64 Token : InputTokenSet)
			{
				if (AliasTokenSet->Contains(Token)) OverlapCount++;
			}

			if (OverlapCount > 0 && InputTokenSet.Num() > 0 && AliasTokenSet->Num() > 0)
			{
				// How much of the player's input was useful? (Punishes extra words)
				float Precision = static_cast<float>(OverlapCount) / static_cast<float>(InputTokenSet.Num());
				
				// How much of the alias was successfully guessed? (Punishes missing words)
				float Recall = static_cast<float>(OverlapCount) / static_cast<float>(AliasTokenSet->Num());

				// Balance without arbitrary penalties
				LexicalF1Score = 2.0f * (Precision * Recall) / (Precision + Recall);
			}
		}
		
		float AdjustedScore = FMath::Clamp((SemanticScore * SemanticWeight) + (LexicalF1Score * LexicalWeight), -1.0f, 1.0f);

#if !UE_BUILD_SHIPPING
		ULog::Trace(TEXT("SemanticParser.cpp - GetBestMatchingCommandReport"), *FString::Printf(TEXT("Alias='%s' Command=%s Sem=%0.4f F1Lexical=%0.4f Final=%0.4f"),
			*AliasText, *UEnum::GetValueAsString(*FoundCommand), SemanticScore, LexicalF1Score, AdjustedScore));
#endif

		const float ExistingCommandBest = BestScorePerCommand.FindRef(*FoundCommand);
		if (!BestScorePerCommand.Contains(*FoundCommand) || AdjustedScore > ExistingCommandBest)
		{
			BestScorePerCommand.Add(*FoundCommand, AdjustedScore);
		}

		if (AdjustedScore > BestScore)
		{
			BestScore = AdjustedScore;
			BestAlias = AliasText;
			WinningCommand = *FoundCommand;
		}
	}

	for (const TPair<ENPCAnimationID, float>& CommandScorePair : BestScorePerCommand)
	{
		if (CommandScorePair.Key == WinningCommand) continue;
		if (CommandScorePair.Value > RunnerUpScore)
		{
			RunnerUpScore = CommandScorePair.Value;
			RunnerUpCommand = CommandScorePair.Key;
		}
	}

	Report.BestAlias = BestAlias;
	Report.BestCommand = WinningCommand;
	Report.BestScore = BestScore;
	Report.RunnerUpScore = RunnerUpScore;
	Report.RunnerUpCommand = RunnerUpCommand;
	Report.Margin = (RunnerUpScore < -0.5f) ? BestScore : (BestScore - RunnerUpScore);
	
	return Report;
}

ENPCAnimationID USemanticParser::GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold, float MinimumMargin)
{
	FScopeLock Lock(&InferenceMutex);

	FSemanticParseScoreReport Report = GetBestMatchingCommandReport(PlayerInput);
	if (Report.BestCommand == ENPCAnimationID::ACTION_NONE) return ENPCAnimationID::ACTION_NONE;
	
	Report.ConfidenceThreshold = ConfidenceThreshold;
	if (Report.BestScore < ConfidenceThreshold)
	{
		ULog::Warning(TEXT("SemanticParser.cpp - GetBestMatchingCommand"), *FString::Printf(TEXT("Rejected! Best match was '%s' for command: '%s' (Score: %f) but fell below threshold of %f"),
		       *Report.BestAlias, *UEnum::GetValueAsString(Report.BestCommand), Report.BestScore, ConfidenceThreshold));
		return ENPCAnimationID::ACTION_NONE;
	}

	Report.MinimumMargin = MinimumMargin;
	if (Report.Margin < MinimumMargin)
	{
		ULog::Error(TEXT("SemanticParser.cpp - GetBestMatchingCommand"), *FString::Printf(TEXT("Rejected! Best match was '%s' (%s, Score: %f) but competing command %s scored %f (Margin: %f, min: %f)"),
		       *Report.BestAlias, *UEnum::GetValueAsString(Report.BestCommand), Report.BestScore, *UEnum::GetValueAsString(Report.RunnerUpCommand), Report.RunnerUpScore, Report.Margin, MinimumMargin));
		return ENPCAnimationID::ACTION_NONE;
	}

	ULog::Info(TEXT("SemanticParser.cpp - GetBestMatchingCommand"), *FString::Printf(TEXT("WINNER: %s via alias '%s' (Score: %f, Margin vs %s: %f)"),
		       *UEnum::GetValueAsString(Report.BestCommand), *Report.BestAlias, Report.BestScore, *UEnum::GetValueAsString(Report.RunnerUpCommand), Report.Margin));

	return Report.BestCommand;
}

FSemanticParseEvaluationReport USemanticParser::EvaluateParsingCases(const TArray<FSemanticParseCase>& TestCases, float ConfidenceThreshold, float MinimumMargin)
{
	FSemanticParseEvaluationReport Evaluation;
	Evaluation.TotalCases = TestCases.Num();

	for (const FSemanticParseCase& TestCase : TestCases)
	{
		FSemanticParseScoreReport Report = GetBestMatchingCommandReport(TestCase.InputText);
		Report.ExpectedCommand = TestCase.ExpectedCommand;
		Evaluation.CaseReports.Add(Report);
		Evaluation.AverageBestScore += Report.BestScore;

		const bool bAccepted = (Report.BestCommand != ENPCAnimationID::ACTION_NONE) && (Report.BestScore >= ConfidenceThreshold) && (Report.Margin >= MinimumMargin);
		const bool bExpectedRejection = (TestCase.ExpectedCommand == ENPCAnimationID::ACTION_NONE);
		const bool bPassed = bExpectedRejection ? !bAccepted : (bAccepted && (Report.BestCommand == TestCase.ExpectedCommand));
		
		Report.bPassed = bPassed;
		Evaluation.CaseReports.Last() = Report;

		if (bExpectedRejection) bPassed ? ++Evaluation.CorrectRejections : ++Evaluation.FalsePositives;
		else bPassed ? ++Evaluation.CorrectMatches : ++Evaluation.FalseNegatives;

		Evaluation.PassedCases += bPassed ? 1 : 0;
		Evaluation.FailedCases += bPassed ? 0 : 1;

#if !UE_BUILD_SHIPPING
		ULog::Trace(TEXT("SemanticParser.cpp - EvaluateParsingCases"), *FString::Printf(TEXT("Detailed report for case '%s': Expected=%s, BestMatch=%s (Alias: '%s', Score: %f), RunnerUp=%s (Score: %f), Margin=%f, Passed=%s"),
		       *TestCase.InputText, *UEnum::GetValueAsString(TestCase.ExpectedCommand), *UEnum::GetValueAsString(Report.BestCommand), *Report.BestAlias, Report.BestScore, *UEnum::GetValueAsString(Report.RunnerUpCommand), Report.RunnerUpScore, Report.Margin, bPassed ? TEXT("true") : TEXT("false")));
#endif
	}

	if (Evaluation.TotalCases > 0) Evaluation.AverageBestScore /= static_cast<float>(Evaluation.TotalCases);
	return Evaluation;
}

FSemanticParseEvaluationReport USemanticParser::EvaluateParsingCasesFromDataTable(UDataTable* EvaluationTable, float ConfidenceThreshold, float MinimumMargin)
{
	if (!EvaluationTable) return FSemanticParseEvaluationReport();

	TArray<FSemanticParseCaseRow*> Rows;
	EvaluationTable->GetAllRows<FSemanticParseCaseRow>(TEXT("SemanticParserEvaluation"), Rows);

	TArray<FSemanticParseCase> Cases;
	Cases.Reserve(Rows.Num());

	for (const FSemanticParseCaseRow* Row : Rows)
	{
		if (!Row) continue;
		FSemanticParseCase Case;
		Case.InputText = Row->InputText;
		Case.ExpectedCommand = Row->ExpectedCommand;
		Cases.Add(Case);
	}

	return EvaluateParsingCases(Cases, ConfidenceThreshold, MinimumMargin);
}

FString USemanticParser::ExportEvaluationReportToJson(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath, bool bPrettyPrint) const
{
	FString JsonOutput;
	const int32 Indent = bPrettyPrint ? 2 : 0;
	if (!FJsonObjectConverter::UStructToJsonObjectString(FSemanticParseEvaluationReport::StaticStruct(), &Report, JsonOutput, 0, 0, Indent)) return TEXT("");
	if (!OutputFilePath.IsEmpty()) FFileHelper::SaveStringToFile(JsonOutput, *OutputFilePath);
	return JsonOutput;
}

FString USemanticParser::ExportEvaluationReportToCsv(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath) const
{
	FString CsvOutput = TEXT("InputText,ExpectedCommand,BestAlias,BestCommand,BestScore,RunnerUpCommand,RunnerUpScore,Margin,bPassed\n");
	for (const FSemanticParseScoreReport& CaseReport : Report.CaseReports)
	{
		CsvOutput += EscapeCsvField(CaseReport.InputText) + TEXT(",");
		CsvOutput += EscapeCsvField(UEnum::GetValueAsString(CaseReport.ExpectedCommand)) + TEXT(",");
		CsvOutput += EscapeCsvField(CaseReport.BestAlias) + TEXT(",");
		CsvOutput += EscapeCsvField(UEnum::GetValueAsString(CaseReport.BestCommand)) + TEXT(",");
		CsvOutput += FString::Printf(TEXT("%0.4f,"), CaseReport.BestScore);
		CsvOutput += EscapeCsvField(UEnum::GetValueAsString(CaseReport.RunnerUpCommand)) + TEXT(",");
		CsvOutput += FString::Printf(TEXT("%0.4f,%0.4f,"), CaseReport.RunnerUpScore, CaseReport.Margin);
		CsvOutput += (CaseReport.bPassed ? TEXT("true") : TEXT("false"));
		CsvOutput += TEXT("\n");
	}
	if (!OutputFilePath.IsEmpty()) FFileHelper::SaveStringToFile(CsvOutput, *OutputFilePath);
	return CsvOutput;
}

FString USemanticParser::GetRandomDialogueOption(ENPCAnimationID CommandID)
{
	for (const auto& Pair : CommandAliasesMap)
	{
		if (Pair.Key == CommandID) return Pair.Value.DialogueOptions[FMath::RandRange(0, Pair.Value.DialogueOptions.Num() - 1)];
	}
	return TEXT("Not found");
}

void USemanticParser::CacheEmbeddingsFromDataTable(UDataTable* CommandTable)
{
	if (!CommandTable) return;

	CachedAliasEmbeddings.Reset();
	AliasToCommandMap.Reset();
	CachedAliasTokenSets.Reset();
	AliasVocabularyTokenSet.Reset();
	CommandAliasesMap.Reset();

	TArray<FCommandAliasRow*> AllRows;
	CommandTable->GetAllRows<FCommandAliasRow>(TEXT("SemanticParserCache"), AllRows);

	for (int32 i = 0; i < AllRows.Num(); ++i)
	{
		ENPCAnimationID CommandID = AllRows[i]->CommandID;
		const TArray<FString>& Aliases = AllRows[i]->Aliases;

		for (const FString& Alias : Aliases)
		{
			TArray<int64> InputIDs;
			if (TokenizeString(Alias, InputIDs))
			{
				TSet<int64> AliasTokenSet = ExtractContentTokenSet(InputIDs);
				for (const int64 AliasTokenId : AliasTokenSet) AliasVocabularyTokenSet.Add(AliasTokenId);

				TArray<float> Embedding = GetSemanticEmbedding(InputIDs);
				if (!Embedding.IsEmpty())
				{
					CachedAliasEmbeddings.Add(Alias, Embedding);
					AliasToCommandMap.Add(Alias, CommandID);
					CachedAliasTokenSets.Add(Alias, MoveTemp(AliasTokenSet));
					CommandAliasesMap.Add(CommandID, *AllRows[i]);
				}
			}
		}
	}
}

bool USemanticParser::TokenizeString(const FString& InputText, TArray<int64>& OutInputIDs) const
{
	if (!TokenizerInstance) return false;
	auto* TokenizerPtr = static_cast<std::unique_ptr<class Tokenizer>*>(TokenizerInstance);
	std::string StdInput = TCHAR_TO_UTF8(*InputText);
	std::vector<int> RawTokens = (*TokenizerPtr)->Encode(StdInput);

	constexpr int32 MaxSequenceLength = 128; 
	OutInputIDs.Init(0, MaxSequenceLength);
	OutInputIDs[0] = 101;

	int32 CurrentIndex = 1;
	for (int i = 0; i < RawTokens.size() && CurrentIndex < MaxSequenceLength - 1; ++i)
	{
		OutInputIDs[CurrentIndex] = static_cast<int64>(RawTokens[i]);
		CurrentIndex++;
	}
	OutInputIDs[CurrentIndex] = 102;
	return true;
}

FString USemanticParser::GetTokenizerFilePath()
{
	FString ContentDir = IPluginManager::Get().FindPlugin("ParsingSystemUEAddon")->GetContentDir();
	FString TokenizerPath = FPaths::Combine(ContentDir, TEXT("NLP_DATA"), TEXT("tokenizer.json"));
	FPaths::CollapseRelativeDirectories(TokenizerPath);
	return TokenizerPath;
}

int32 USemanticParser::CountContentTokens(const TArray<int64>& TokenIds) const
{
	int32 Count = 0;
	for (const int64 TokenId : TokenIds) if (IsContentToken(TokenId)) ++Count;
	return Count;
}

TSet<int64> USemanticParser::ExtractContentTokenSet(const TArray<int64>& TokenIds) const
{
	TSet<int64> ContentSet;
	for (const int64 TokenId : TokenIds) if (IsContentToken(TokenId)) ContentSet.Add(TokenId);
	return ContentSet;
}

TArray<int64> USemanticParser::BuildFocusedInputIDs(const TArray<int64>& TokenIds, const TSet<int64>& AliasVocabulary) const
{
	if (AliasVocabulary.IsEmpty()) return TokenIds;

	TArray<int64> Focused;
	Focused.Reserve(TokenIds.Num());
	Focused.Add(CLSTokenId);

	for (const int64 TokenId : TokenIds)
	{
		if (IsContentToken(TokenId) && AliasVocabulary.Contains(TokenId)) Focused.Add(TokenId);
	}

	Focused.Add(SEPTokenId);
	while (Focused.Num() < TokenIds.Num()) Focused.Add(PadTokenId);
	return Focused;
}

FString USemanticParser::EscapeCsvField(const FString& Field)
{
	FString Escaped = Field;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}