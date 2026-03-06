// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "SemanticParser.generated.h"
/**
 * A multifunctional system which:
 * 1. Initializes and loads a reranker model (all-MiniLM-L6-v2-onnx) through the Unreal Engine's Neural Network Engine
 * 2. Tokenizes sentences via the tokenizers-cpp third-party .h and .lib files
 * 3. Takes care of the cosine similarity calculations
 * 4. Returns the best matching command in FString format
 */
USTRUCT(BlueprintType)
struct FAliasList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Semantic Parsing")
	TArray<FString> Aliases;
};

UCLASS()
class USemanticParser : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	bool InitializeModel(UNNEModelData* InModelData);
	
	UFUNCTION(BlueprintPure, Category = "Semantic Parsing")
	static float CalculateCosineSimilarity(const TArray<float>& VectorA, const TArray<float>& VectorB);
	
	// Takes the token and returns the 384-dimensional embedding vector
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	TArray<float> GetSemanticEmbedding(const TArray<int64>& InputIDs, const TArray<int64>& AttentionMask) const;
	
	// Load the tokenizer.json file into memory
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	bool InitializeTokenizer();
	
	// Calculate and save the embeddings for the predefined commands
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	void CacheCommandEmbeddings(const TMap<FString, FAliasList>& CommandAliases);
	
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold = 0.8f);
	
private:
	// The compiled ONNX Model ready for CPU execution
	TSharedPtr<UE::NNE::IModelInstanceCPU> ModelInstance;
	
	// Stores the pre-calculated vectors: Command string -> 384-Float Array
	TMap<FString, TArray<float>> CachedCommandsEmbeddings;
	
	// The use of raw void pointer here is so that there won't be a need to #include standard
	// C++ headers into the Unreal header file, preventing compiler errors
	void* TokenizerInstance = nullptr;
	
	// Internal helper for the actual text-to-ID conversion
	bool TokenizeString(const FString& InputText, TArray<int64>& OutInputIDs, TArray<int64>& OutAttentionMask) const;
	
	// Maps a natural sentence directly to its math vector (e.g., "Lie down" -> [0.1, 0.4...])
	TMap<FString, TArray<float>> CachedAliasEmbeddings;

	// Maps that natural sentence back to the parent command (e.g., "Lie down" -> "ACTION_ONTHEGROUND")
	TMap<FString, FString> AliasToCommandMap;

	static FString GetTokenizerFilePath();
};
