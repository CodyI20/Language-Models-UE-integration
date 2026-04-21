// Fill out your copyright notice in the Description page of Project Settings.

#include "SemanticParser.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"

// Protecting third party includes
THIRD_PARTY_INCLUDES_START
#include "tokenizers_cpp.h"
THIRD_PARTY_INCLUDES_END

// Unnamed (Anonymous) namespace to prevent linkage errors in case any other .cpp file in the project ever uses the exact same variable name
namespace
{
	constexpr int32 EmbeddingDimension = 384;
	constexpr int64 PadTokenId = 0;
	constexpr int64 CLSTokenId = 101;
	constexpr int64 SEPTokenId = 102;

	// Switch this to Aggressive for stronger single-word containment behavior.
	enum class EParserScorePreset : uint8
	{
		Safe,
		Aggressive
	};

	EParserScorePreset GetActiveScorePreset()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("ParsingAggressive"))
			? EParserScorePreset::Aggressive
			: EParserScorePreset::Safe;
	}

	const bool bAggressivePreset = GetActiveScorePreset() == EParserScorePreset::Aggressive;
	constexpr float SafeAliasContainedBoost = 0.24f;
	constexpr float SafeInputContainedBoost = 0.18f;
	constexpr float SafeCoverageBlendWeight = 0.10f;
	constexpr float SafeMaxLexicalBoost = 0.33f;
	constexpr float AggressiveAliasContainedBoost = 0.30f;
	constexpr float AggressiveInputContainedBoost = 0.22f;
	constexpr float AggressiveCoverageBlendWeight = 0.12f;
	constexpr float AggressiveMaxLexicalBoost = 0.42f;
	const float AliasContainedBoost = bAggressivePreset ? AggressiveAliasContainedBoost : SafeAliasContainedBoost;
	const float InputContainedBoost = bAggressivePreset ? AggressiveInputContainedBoost : SafeInputContainedBoost;
	const float CoverageBlendWeight = bAggressivePreset ? AggressiveCoverageBlendWeight : SafeCoverageBlendWeight;
	const float MaxLexicalBoost = bAggressivePreset ? AggressiveMaxLexicalBoost : SafeMaxLexicalBoost;

	bool IsContentToken(const int64 TokenId)
	{
		return TokenId != PadTokenId && TokenId != CLSTokenId && TokenId != SEPTokenId;
	}

	int32 CountContentTokens(const TArray<int64>& TokenIds)
	{
		int32 Count = 0;
		for (const int64 TokenId : TokenIds)
		{
			if (IsContentToken(TokenId))
			{
				++Count;
			}
		}
		return Count;
	}

	TSet<int64> ExtractContentTokenSet(const TArray<int64>& TokenIds)
	{
		TSet<int64> ContentSet;
		for (const int64 TokenId : TokenIds)
		{
			if (IsContentToken(TokenId))
			{
				ContentSet.Add(TokenId);
			}
		}
		return ContentSet;
	}

	TArray<int64> BuildFocusedInputIDs(const TArray<int64>& TokenIds, const TSet<int64>& AliasVocabulary)
	{
		if (AliasVocabulary.IsEmpty())
		{
			return TokenIds;
		}

		TArray<int64> Focused;
		Focused.Reserve(TokenIds.Num());
		Focused.Add(CLSTokenId);

		for (const int64 TokenId : TokenIds)
		{
			if (IsContentToken(TokenId) && AliasVocabulary.Contains(TokenId))
			{
				Focused.Add(TokenId);
			}
		}

		Focused.Add(SEPTokenId);
		while (Focused.Num() < TokenIds.Num())
		{
			Focused.Add(PadTokenId);
		}

		return Focused;
	}

	float ComputeLexicalBoost(const TSet<int64>& InputTokenSet, const TSet<int64>& AliasTokenSet)
	{
		if (InputTokenSet.IsEmpty() || AliasTokenSet.IsEmpty())
		{
			return 0.0f;
		}

		int32 OverlapCount = 0;
		for (const int64 AliasToken : AliasTokenSet)
		{
			if (InputTokenSet.Contains(AliasToken))
			{
				++OverlapCount;
			}
		}

		if (OverlapCount == 0)
		{
			return 0.0f;
		}

		const float AliasCoverage = static_cast<float>(OverlapCount) / static_cast<float>(AliasTokenSet.Num());
		const float InputCoverage = static_cast<float>(OverlapCount) / static_cast<float>(InputTokenSet.Num());

		float Boost = 0.0f;
		if (AliasCoverage >= 1.0f)
		{
			Boost += AliasContainedBoost;
		}
		if (InputCoverage >= 1.0f)
		{
			Boost += InputContainedBoost;
		}

		const float WeightedCoverage = (0.7f * AliasCoverage) + (0.3f * InputCoverage);
		Boost += CoverageBlendWeight * WeightedCoverage;

		return FMath::Min(Boost, MaxLexicalBoost);
	}

	FString EscapeCsvField(const FString& Field)
	{
		FString Escaped = Field;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}
}

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
	UE_LOG(LogTemp, Log, TEXT("Initializing SemanticParser"));
	
	FString AssetPath = TEXT("/ParsingSystemUEAddon/LMModel/all-MiniLM-L6-v2-onnx.all-MiniLM-L6-v2-onnx");
	UNNEModelData* FoundModel = LoadObject<UNNEModelData>(nullptr, *AssetPath);
	
	if (!FoundModel)
	{
		UE_LOG(LogTemp, Error, TEXT("Model not loaded at path: %s"), *AssetPath);
		return false;
	}
	
	// Get the ONNX CPU Runtime
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(FString("NNERuntimeORTCpu"));
	
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("NNE ONNX CPU Runtime is not valid."));
		return false;
	}
	
	// Create the Model and the Instance
	TSharedPtr<UE::NNE::IModelCPU> Model = Runtime -> CreateModelCPU(FoundModel);
	if (Model.IsValid())
	{
		ModelInstance = Model -> CreateModelInstanceCPU();
		UE_LOG(LogTemp, Display, TEXT("Model loaded successfully!"));
		return ModelInstance.IsValid();
	}
	
	return false;
}

bool USemanticParser::InitializeTokenizer()
{
	UE_LOG(LogTemp, Log, TEXT("Initializing the tokenizer"));

	FString TokenizerFilePath = GetTokenizerFilePath();
    
	// Read the text content of the file into an FString
	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *TokenizerFilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to read the file contents at: %s"), *TokenizerFilePath);
		return false;
	}
    
	// Convert the raw JSON content to a standard C++ string
	std::string StdJsonBlob = TCHAR_TO_UTF8(*JsonContent);

	if (auto Tokenizer = Tokenizer::FromBlobJSON(StdJsonBlob))
	{
		TokenizerInstance = new std::unique_ptr<class Tokenizer>(std::move(Tokenizer));
		return true;
	}
    
	UE_LOG(LogTemp, Error, TEXT("Failed to parse Tokenizer JSON data."));
	return false;
}

TArray<float> USemanticParser::GetSemanticEmbedding(const TArray<int64>& InputIDs) const
{
    if (!ModelInstance.IsValid()) return TArray<float>();
	
    TArray<int64> CleanInputIDs;
    TArray<int64> CleanAttentionMask;

    for (int32 i = 0; i < InputIDs.Num(); ++i)
    {
        // In BERT vocabulary, 0 is strictly the [PAD] token which messes up the math
        if (InputIDs[i] != 0) 
        {
            CleanInputIDs.Add(InputIDs[i]);
            CleanAttentionMask.Add(1); // Forces a perfect attention mask for the real words
        }
    }

    // Safety check just in case an empty string was passed
    if (CleanInputIDs.IsEmpty()) return TArray<float>();

    int32 NumInputs = ModelInstance->GetInputTensorDescs().Num();
    
    // Create Token Type IDs to match the new, shrunken size
    TArray<int64> TokenTypeIDs;
    TokenTypeIDs.Init(0, CleanInputIDs.Num());

    TArray<UE::NNE::FTensorBindingCPU> InputBindings;
    InputBindings.SetNumZeroed(NumInputs);

    TArray<UE::NNE::FTensorShape> InputShapes;
    InputShapes.SetNum(NumInputs);

    TConstArrayView<UE::NNE::FTensorDesc> InputDescs = ModelInstance->GetInputTensorDescs();

    // Bind inputs by their actual names
    for (int32 i = 0; i < NumInputs; ++i)
    {
        FString InputName = InputDescs[i].GetName();

        if (InputName.Contains(TEXT("input_ids")))
        {
            InputBindings[i].Data = static_cast<void*>(CleanInputIDs.GetData());
            InputBindings[i].SizeInBytes = CleanInputIDs.Num() * sizeof(int64);
            InputShapes[i] = UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(CleanInputIDs.Num()) });
        }
        else if (InputName.Contains(TEXT("attention_mask")))
        {
            InputBindings[i].Data = static_cast<void*>(CleanAttentionMask.GetData());
            InputBindings[i].SizeInBytes = CleanAttentionMask.Num() * sizeof(int64);
            InputShapes[i] = UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(CleanAttentionMask.Num()) });
        }
        else if (InputName.Contains(TEXT("token_type_ids")))
        {
            InputBindings[i].Data = static_cast<void*>(TokenTypeIDs.GetData());
            InputBindings[i].SizeInBytes = TokenTypeIDs.Num() * sizeof(int64);
            InputShapes[i] = UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(TokenTypeIDs.Num()) });
        }
    }

    if (ModelInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok) return TArray<float>();

    // Math is now based ONLY on the real words
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

        // Mean Pooling: Skip CLS (index 0) and SEP (index SeqLen - 1)
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
        else // Fallback if the array only has 1 or 2 tokens for some reason
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
    	for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim)
    	{
    		Magnitude += FinalEmbedding[Dim] * FinalEmbedding[Dim];
    	}
    	
    	if (Magnitude > 0.0f)
    	{
    		float SqrtMagnitude = FMath::Sqrt(Magnitude);
    		float InvMag = 1.0f / SqrtMagnitude;
    		for (int32 Dim = 0; Dim < EmbeddingDimension; ++Dim)
    		{
    			FinalEmbedding[Dim] *= InvMag;
    		}
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
		UE_LOG(LogTemp, Error, TEXT("Error: Cache is empty!"));
		return Report;
	};

	TArray<int64> InputIDs;
	
	if (!TokenizeString(PlayerInput, InputIDs))
	{
		UE_LOG(LogTemp, Error, TEXT("Error: Tokenization Failed"));
		return Report;
	};
#if !UE_BUILD_SHIPPING
	FString TokenString = TEXT("");
	
	for (int64 TokenID : InputIDs)
	{
		TokenString += FString::Printf(TEXT("%lld "), TokenID);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Raw tokens for '%s': [ %s]"), *PlayerInput, *TokenString);
#endif
	

	TArray<float> PlayerEmbedding = GetSemanticEmbedding(InputIDs);
	if (PlayerEmbedding.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Error: Embedding Failed"));
		return Report;
	};

	// Build a noise-resistant embedding variant by keeping only tokens seen in aliases.
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
	FString BestAlias = TEXT("None");
	float BestScore = -1.0f;
	float RunnerUpScore = -1.0f;

	// Compare the input text against every cached phrase
	for (const auto& CachedPair : CachedAliasEmbeddings)
	{
		const FString& AliasText = CachedPair.Key;
		const TArray<float>& AliasVector = CachedPair.Value;

		float SimilarityScore = 0.0f;
		for (int32 i = 0; i < EmbeddingDimension; ++i)
		{
			SimilarityScore += PlayerEmbedding[i] * AliasVector[i];
		}
		const float RawSimilarityScore = SimilarityScore;

		float FocusedSimilarityScore = SimilarityScore;

		if (!FocusedPlayerEmbedding.IsEmpty())
		{
			FocusedSimilarityScore = 0.0f;
			for (int32 i = 0; i < EmbeddingDimension; ++i)
			{
				FocusedSimilarityScore += FocusedPlayerEmbedding[i] * AliasVector[i];
			}
			SimilarityScore = FMath::Max(SimilarityScore, FocusedSimilarityScore);
		}

		float LexicalBoost = 0.0f;
		if (const TSet<int64>* AliasTokenSet = CachedAliasTokenSets.Find(AliasText))
		{
			LexicalBoost = ComputeLexicalBoost(InputTokenSet, *AliasTokenSet);
		}

		const float AdjustedScore = FMath::Clamp(SimilarityScore + LexicalBoost, -1.0f, 1.0f);
#if !UE_BUILD_SHIPPING
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("Alias='%s' RawSemantic=%0.4f FocusedSemantic=%0.4f ChosenSemantic=%0.4f Lexical=%0.4f Final=%0.4f"),
			*AliasText,
			RawSimilarityScore,
			FocusedSimilarityScore,
			SimilarityScore,
			LexicalBoost,
			AdjustedScore
		);
#endif

		if (AdjustedScore > BestScore)
		{
			RunnerUpScore = BestScore;
			BestScore = AdjustedScore;
			BestAlias = AliasText;
			if (const auto* FoundCommand = AliasToCommandMap.Find(AliasText))
			{
				WinningCommand = *FoundCommand;
			}
		}
		else if (AdjustedScore > RunnerUpScore)
		{
			RunnerUpScore = AdjustedScore;
		}
	}

	Report.BestAlias = BestAlias;
	Report.BestCommand = WinningCommand;
	Report.BestScore = BestScore;
	Report.RunnerUpScore = RunnerUpScore;
	Report.Margin = (RunnerUpScore < -0.5f) ? BestScore : (BestScore - RunnerUpScore);

	return Report;
}

ENPCAnimationID USemanticParser::GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold, float MinimumMargin)

{
	// Lock the function until the thread is done
	FScopeLock Lock(&InferenceMutex);

	const FSemanticParseScoreReport Report = GetBestMatchingCommandReport(PlayerInput);
	if (Report.BestCommand == ENPCAnimationID::ACTION_NONE)
	{
		return ENPCAnimationID::ACTION_NONE;
	}

	if (Report.BestScore < ConfidenceThreshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("Rejected! Best match was '%s' (Score: %f) but fell below threshold of %f"), *Report.BestAlias, Report.BestScore, ConfidenceThreshold);
		return ENPCAnimationID::ACTION_NONE;
	}

	if (Report.Margin < MinimumMargin)
	{
		UE_LOG(LogTemp, Warning, TEXT("Rejected! Best match was '%s' (Score: %f, Margin: %f) but fell below minimum margin of %f"), *Report.BestAlias, Report.BestScore, Report.Margin, MinimumMargin);
		return ENPCAnimationID::ACTION_NONE;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("WINNER: %s via alias '%s' (Score: %f, Margin: %f)"), *UEnum::GetValueAsString(Report.BestCommand), *Report.BestAlias, Report.BestScore, Report.Margin);
#if !UE_BUILD_SHIPPING
	static bool bLoggedScorePreset = false;
	if (!bLoggedScorePreset)
	{
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("Semantic parser score preset: %s"),
			bAggressivePreset ? TEXT("Aggressive") : TEXT("Safe")
		);
		bLoggedScorePreset = true;
	}
#endif
    
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
		Report.bShouldMatch = TestCase.bShouldMatch;
		Evaluation.CaseReports.Add(Report);
		Evaluation.AverageBestScore += Report.BestScore;

		const bool bAccepted = (Report.BestCommand != ENPCAnimationID::ACTION_NONE) && (Report.BestScore >= ConfidenceThreshold) && (Report.Margin >= MinimumMargin);
		const bool bPassed = TestCase.bShouldMatch ? (bAccepted && (Report.BestCommand == TestCase.ExpectedCommand)) : !bAccepted;
		Report.bPassed = bPassed;
		Evaluation.CaseReports.Last() = Report;

		if (TestCase.bShouldMatch)
		{
			if (bPassed)
			{
				++Evaluation.CorrectMatches;
			}
			else
			{
				++Evaluation.FalseNegatives;
			}
		}
		else
		{
			if (bPassed)
			{
				++Evaluation.CorrectRejections;
			}
			else
			{
				++Evaluation.FalsePositives;
			}
		}

		Evaluation.PassedCases += bPassed ? 1 : 0;
		Evaluation.FailedCases += bPassed ? 0 : 1;

#if !UE_BUILD_SHIPPING
		UE_LOG(
			LogTemp,
			Verbose,
			TEXT("EvalCase='%s' Expected=%s bShouldMatch=%s Actual=%s Accepted=%s BestScore=%0.4f Margin=%0.4f Result=%s"),
			*TestCase.InputText,
			*UEnum::GetValueAsString(TestCase.ExpectedCommand),
			TestCase.bShouldMatch ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Report.BestCommand),
			bAccepted ? TEXT("true") : TEXT("false"),
			Report.BestScore,
			Report.Margin,
			bPassed ? TEXT("PASS") : TEXT("FAIL")
		);
#endif
	}

	if (Evaluation.TotalCases > 0)
	{
		Evaluation.AverageBestScore /= static_cast<float>(Evaluation.TotalCases);
	}

	return Evaluation;
}

FSemanticParseEvaluationReport USemanticParser::EvaluateParsingCasesFromDataTable(UDataTable* EvaluationTable, float ConfidenceThreshold, float MinimumMargin)
{
	if (!EvaluationTable)
	{
		UE_LOG(LogTemp, Error, TEXT("Evaluation table is missing or invalid!"));
		return FSemanticParseEvaluationReport();
	}

	TArray<FSemanticParseCaseRow*> Rows;
	EvaluationTable->GetAllRows<FSemanticParseCaseRow>(TEXT("SemanticParserEvaluation"), Rows);

	TArray<FSemanticParseCase> Cases;
	Cases.Reserve(Rows.Num());

	for (const FSemanticParseCaseRow* Row : Rows)
	{
		if (!Row)
		{
			continue;
		}

		FSemanticParseCase Case;
		Case.InputText = Row->InputText;
		Case.ExpectedCommand = Row->ExpectedCommand;
		Case.bShouldMatch = Row->bShouldMatch;
		Cases.Add(Case);
	}

	return EvaluateParsingCases(Cases, ConfidenceThreshold, MinimumMargin);
}

FString USemanticParser::ExportEvaluationReportToJson(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath, bool bPrettyPrint) const
{
	FString JsonOutput;
	const int32 Indent = bPrettyPrint ? 2 : 0;
	if (!FJsonObjectConverter::UStructToJsonObjectString(FSemanticParseEvaluationReport::StaticStruct(), &Report, JsonOutput, 0, 0, Indent))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to convert evaluation report to JSON."));
		return TEXT("");
	}

	if (!OutputFilePath.IsEmpty())
	{
		if (!FFileHelper::SaveStringToFile(JsonOutput, *OutputFilePath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to save evaluation JSON to: %s"), *OutputFilePath);
		}
	}

	return JsonOutput;
}

FString USemanticParser::ExportEvaluationReportToCsv(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath) const
{
	FString CsvOutput;
	CsvOutput += TEXT("InputText,ExpectedCommand,bShouldMatch,BestAlias,BestCommand,BestScore,RunnerUpScore,Margin,bPassed\n");

	for (const FSemanticParseScoreReport& CaseReport : Report.CaseReports)
	{
		CsvOutput += EscapeCsvField(CaseReport.InputText) + TEXT(",");
		CsvOutput += EscapeCsvField(UEnum::GetValueAsString(CaseReport.ExpectedCommand)) + TEXT(",");
		CsvOutput += FString::Printf(TEXT("%s,"), CaseReport.bShouldMatch ? TEXT("true") : TEXT("false"));
		CsvOutput += EscapeCsvField(CaseReport.BestAlias) + TEXT(",");
		CsvOutput += EscapeCsvField(UEnum::GetValueAsString(CaseReport.BestCommand)) + TEXT(",");
		CsvOutput += FString::Printf(TEXT("%0.4f,%0.4f,%0.4f,"), CaseReport.BestScore, CaseReport.RunnerUpScore, CaseReport.Margin);
		CsvOutput += (CaseReport.bPassed ? TEXT("true") : TEXT("false"));
		CsvOutput += TEXT("\n");
	}

	if (!OutputFilePath.IsEmpty())
	{
		if (!FFileHelper::SaveStringToFile(CsvOutput, *OutputFilePath))
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to save evaluation CSV to: %s"), *OutputFilePath);
		}
	}

	return CsvOutput;
}

FString USemanticParser::GetRandomDialogueOption(ENPCAnimationID CommandID)
{
	for (const auto& Pair : CommandAliasesMap)
	{
		if (Pair.Key == CommandID)
		{
			return Pair.Value.DialogueOptions[FMath::RandRange(0, Pair.Value.DialogueOptions.Num() - 1)];
		}
	}
	
	return TEXT("Not found");
}

void USemanticParser::CacheEmbeddingsFromDataTable(UDataTable* CommandTable)
{
	if (!CommandTable)
	{
		UE_LOG(LogTemp, Error, TEXT("Data Table is missing or is invalid!"));
		// On-screen
		if (GEngine)
		{
			const FString DebugMessage = FString::Printf(TEXT("Data table is missing! Please provide one in the parsing system actor component!\n"
											   "Note that the system won't work without one"));
			GEngine->AddOnScreenDebugMessage(256,
				100.f,
				FColor::Red, 
				TEXT("Data table is missing! Please provide one in the parsing system actor component!\n"
											   "Note that the system won't work without one"), 
				false);
		}
		return;
	}
	
	CachedAliasEmbeddings.Reset();
	AliasToCommandMap.Reset();
	CachedAliasTokenSets.Reset();
	AliasVocabularyTokenSet.Reset();
	CommandAliasesMap.Reset();
	
	TArray<FCommandAliasRow*> AllRows;
	CommandTable->GetAllRows<FCommandAliasRow>(TEXT("SemanticParserCache"), AllRows);
	
	for (int32 i=0; i<AllRows.Num(); ++i)
	{
		ENPCAnimationID CommandID = AllRows[i]->CommandID;
		const TArray<FString>& Aliases = AllRows[i] -> Aliases;
		
		for (const FString& Alias : Aliases)
		{
			TArray<int64> InputIDs;
			
			if (TokenizeString(Alias, InputIDs))
			{
				TSet<int64> AliasTokenSet = ExtractContentTokenSet(InputIDs);
				for (const int64 AliasTokenId : AliasTokenSet)
				{
					AliasVocabularyTokenSet.Add(AliasTokenId);
				}

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
	UE_LOG(LogTemp, Warning, TEXT("Data table SUCCESS: Loaded %d aliases into memory from Data Table."), CachedAliasEmbeddings.Num());
}

bool USemanticParser::TokenizeString(const FString& InputText, TArray<int64>& OutInputIDs) const
{
	if (!TokenizerInstance) return false;
	
	// Cast the void pointer back to the actual library object
	auto* TokenizerPtr = static_cast<std::unique_ptr<class Tokenizer>*>(TokenizerInstance);
	
	std::string StdInput = TCHAR_TO_UTF8(*InputText);
	
	// Encode the string
	std::vector<int> RawTokens = (*TokenizerPtr) -> Encode(StdInput);

	constexpr int32 MaxSequenceLength = 128; // Standard for all-MiniLM-L6-v2
	OutInputIDs.Init(0,MaxSequenceLength);
	
	// all-MiniLM-L6-v2 requires [CLS] (101) at the start and [SEP] (102) at the end
	OutInputIDs[0]=101;
	
	int32 CurrentIndex = 1;
	for (int i=0; i<RawTokens.size() && CurrentIndex<MaxSequenceLength - 1; ++i)
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
	
	// Optional step but ensures no oddities happen with relative directories for example: Folder/../Folder by collapsing them
	FPaths::CollapseRelativeDirectories(TokenizerPath);
	
	// Verify if the file exists before trying to load it
	if (!IFileManager::Get().FileExists(*TokenizerPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Tokenizer file not found: %s"), *TokenizerPath);
	}
	
	return TokenizerPath;
}

