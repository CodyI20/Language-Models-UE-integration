// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NNERuntimeCPU.h"
#include "Engine/DataTable.h"
#include "SemanticParser.generated.h"

UENUM(BlueprintType)
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

	UPROPERTY()
	FString InputText;

	UPROPERTY()
	FString BestAlias;

	UPROPERTY()
	ENPCAnimationID BestCommand = ENPCAnimationID::ACTION_NONE;

	UPROPERTY()
	float BestScore = -1.0f;

	UPROPERTY()
	float RunnerUpScore = -1.0f;

	UPROPERTY()
	ENPCAnimationID RunnerUpCommand = ENPCAnimationID::ACTION_NONE;

	UPROPERTY()
	float Margin = 0.0f;

	UPROPERTY()
	ENPCAnimationID ExpectedCommand = ENPCAnimationID::ACTION_NONE;

	UPROPERTY()
	bool bPassed = false;
	
	UPROPERTY()
	float ConfidenceThreshold = 0.0f;
	
	UPROPERTY()
	float MinimumMargin = 0.0f;
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

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	TArray<float> GetSemanticEmbedding(const TArray<int64>& InputIDs) const;

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	bool InitializeForTesting();

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	void CacheEmbeddingsFromDataTable(UDataTable* CommandTable);

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	ENPCAnimationID GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold = 0.70f, float MinimumMargin = 0.05f);

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FSemanticParseScoreReport GetBestMatchingCommandReport(const FString& PlayerInput) const;

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FSemanticParseEvaluationReport EvaluateParsingCases(const TArray<FSemanticParseCase>& TestCases, float ConfidenceThreshold = 0.70f, float MinimumMargin = 0.05f);

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing", meta = (DisplayName = "Run Semantic Parser DataTable Evaluation"))
	FSemanticParseEvaluationReport EvaluateParsingCasesFromDataTable(UDataTable* EvaluationTable, float ConfidenceThreshold = 0.70f, float MinimumMargin = 0.05f);

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString ExportEvaluationReportToJson(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath = TEXT(""), bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString ExportEvaluationReportToCsv(const FSemanticParseEvaluationReport& Report, const FString& OutputFilePath = TEXT("")) const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString GetRandomDialogueOption(ENPCAnimationID CommandID);

private:
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;
	void* TokenizerInstance = nullptr;

	TMap<FString, TArray<float>> CachedAliasEmbeddings;
	TMap<FString, ENPCAnimationID> AliasToCommandMap;
	TMap<FString, TSet<int64>> CachedAliasTokenSets;
	TSet<int64> AliasVocabularyTokenSet;
	TMap<ENPCAnimationID, FCommandAliasRow> CommandAliasesMap;

	FCriticalSection InferenceMutex;

	bool TokenizeString(const FString& InputText, TArray<int64>& OutInputIDs) const;
	bool InitializeTokenizer();
	bool InitializeModel();
	static FString GetTokenizerFilePath();

	const int32 EmbeddingDimension = 384;
	const int64 PadTokenId = 0;
	const int64 CLSTokenId = 101;
	const int64 SEPTokenId = 102;

	bool IsContentToken(const int64 TokenId) const
	{
		return TokenId != PadTokenId && TokenId != CLSTokenId && TokenId != SEPTokenId;
	}

	int32 CountContentTokens(const TArray<int64>& TokenIds) const;
	TSet<int64> ExtractContentTokenSet(const TArray<int64>& TokenIds) const;
	TArray<int64> BuildFocusedInputIDs(const TArray<int64>& TokenIds, const TSet<int64>& AliasVocabulary) const;
	static FString EscapeCsvField(const FString& Input);
};