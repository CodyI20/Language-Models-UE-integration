// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "SemanticParser.generated.h"
/**
 * 
 */
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
	void CacheCommandEmbeddings(const TArray<FString>& PredefinedCommands);
	
	UFUNCTION(BlueprintCallable, Category = "Semantic Parsing")
	FString GetBestMatchingCommand(const FString& PlayerInput);
	
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

	static FString GetTokenizerFilePath();
};
