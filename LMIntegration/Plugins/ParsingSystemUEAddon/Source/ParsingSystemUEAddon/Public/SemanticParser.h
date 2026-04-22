// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NNERuntimeCPU.h"
#include "Engine/DataTable.h"
#include "SemanticParser.generated.h"
/**
 * A multifunctional system which:
 * 1. Initializes and loads a reranker model (all-MiniLM-L6-v2-onnx) through the Unreal Engine's Neural Network Engine
 * 2. Tokenizes sentences via the tokenizers-cpp third-party .h and .lib files
 * 3. Takes care of the cosine similarity calculations
 * 4. Returns the best matching command in FString format
 */

UENUM(BLueprintType)
enum class ENPCAnimationID : uint8
{
	ACTION_GROUND = 0 UMETA(DisplayName = "Ground"),
	ACTION_HANDS_UP = 1 UMETA(DisplayName = "Hands up"),
	ACTION_HANDS_BACK = 2 UMETA(DisplayName = "Hands back"),
	ACTION_NONE = 4 UMETA(DisplayName = "None"),
};

USTRUCT(BlueprintType)
struct FAliasList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	TArray<FString> Aliases;
};

USTRUCT(BlueprintType)
struct FCommandAliasRow : public FTableRowBase
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	ENPCAnimationID CommandID = ENPCAnimationID::ACTION_NONE;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	TArray<FString> Aliases;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	TArray<FString> DialogueOptions;
};

USTRUCT(BlueprintType)
struct FSemanticParseCase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	FString InputText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	ENPCAnimationID ExpectedCommand = ENPCAnimationID::ACTION_NONE;
};

USTRUCT(BlueprintType)
struct FSemanticParseCaseRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	FString InputText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	ENPCAnimationID ExpectedCommand = ENPCAnimationID::ACTION_NONE;
};

USTRUCT(BlueprintType)
struct FSemanticParseScoreReport
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	FString InputText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	FString BestAlias;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	ENPCAnimationID BestCommand = ENPCAnimationID::ACTION_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	float BestScore = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	float RunnerUpScore = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	ENPCAnimationID RunnerUpCommand = ENPCAnimationID::ACTION_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	float Margin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	ENPCAnimationID ExpectedCommand = ENPCAnimationID::ACTION_NONE;

	UPROPERTY()
	bool bPassed = false;
};

USTRUCT(BlueprintType)
struct FSemanticParseEvaluationReport
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 TotalCases = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 PassedCases = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 FailedCases = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 CorrectMatches = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 CorrectRejections = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 FalsePositives = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	int32 FalseNegatives = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	float AverageBestScore = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	TArray<FSemanticParseScoreReport> CaseReports;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTextSent, const FString&, TextSent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCommandProcessed, const ENPCAnimationID&, CommandID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueProcessed, const FString&, DialogueText);

UCLASS()
class USemanticParser : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Category = "Semantic Parsing")
	FOnCommandProcessed OnCommandProcessed;
	
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Category = "Semantic Parsing")
	FOnDialogueProcessed OnDialogueProcessed;
	
	UPROPERTY(BlueprintCallable, BlueprintAssignable, Category = "Semantic Parsing")
	FOnTextSent OnTextSent;
	
	// Takes the token and returns the 384-dimensional embedding vector
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	TArray<float> GetSemanticEmbedding(const TArray<int64>& InputIDs) const;

	// Initializes the parser outside of a subsystem lifecycle, useful for tests and editor tools.
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	bool InitializeForTesting();
	
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	void CacheEmbeddingsFromDataTable(UDataTable* CommandTable);
	
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	ENPCAnimationID GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold = 0.8f, float MinimumMargin = 0.08f);

	// Returns the detailed score breakdown for a single input.
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FSemanticParseScoreReport GetBestMatchingCommandReport(const FString& PlayerInput) const;

	// Runs a labeled evaluation suite and returns aggregate pass/fail metrics.
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FSemanticParseEvaluationReport EvaluateParsingCases(const TArray<FSemanticParseCase>& TestCases, float ConfidenceThreshold = 0.8f, float MinimumMargin = 0.08f);

	// Runs a labeled evaluation suite stored in a DataTable.
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing", meta = (DisplayName = "Run Semantic Parser DataTable Evaluation"))
	FSemanticParseEvaluationReport EvaluateParsingCasesFromDataTable(UDataTable* EvaluationTable, float ConfidenceThreshold = 0.8f, float MinimumMargin = 0.08f);

	// Serializes an evaluation report to JSON; optionally writes it to disk.
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString ExportEvaluationReportToJson(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath = TEXT(""), bool bPrettyPrint = true) const;

	// Serializes an evaluation report to CSV; optionally writes it to disk.
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString ExportEvaluationReportToCsv(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath = TEXT("")) const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString GetRandomDialogueOption(ENPCAnimationID CommandID);
	

private:
	// The compiled ONNX Model ready for CPU execution
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;
	
	// Stores the pre-calculated vectors: Command string -> 384-Float Array
	TMap<FString, TArray<float>> CachedCommandsEmbeddings;
	
	// The use of raw void pointer here is so that there won't be a need to #include standard
	// C++ headers in the Unreal header file, preventing compiler errors
	void* TokenizerInstance = nullptr;
	
	// Internal helper for the actual text-to-ID conversion
	bool TokenizeString(const FString& InputText, TArray<int64>& OutInputIDs) const;
	
	// Maps a natural sentence directly to its math vector (e.g., "Lie down" -> [0.1, 0.4...])
	TMap<FString, TArray<float>> CachedAliasEmbeddings;

	// Maps that natural sentence back to the parent command (e.g., "Lie down" -> "ACTION_ONTHEGROUND")
	TMap<FString, ENPCAnimationID> AliasToCommandMap;

	// Cached content-token sets per alias for lexical overlap boosting.
	TMap<FString, TSet<int64>> CachedAliasTokenSets;

	// Union of all alias content tokens; used to filter out vocative noise (e.g., names).
	TSet<int64> AliasVocabularyTokenSet;
	
	TMap<ENPCAnimationID, FCommandAliasRow> CommandAliasesMap;
	
	// Mutex lock to enhance thread-safe operations for the LM
	FCriticalSection InferenceMutex;
	
	// Load the tokenizer.json file into memory
	bool InitializeTokenizer();
	
	bool InitializeModel();
	
	static FString GetTokenizerFilePath();
};
