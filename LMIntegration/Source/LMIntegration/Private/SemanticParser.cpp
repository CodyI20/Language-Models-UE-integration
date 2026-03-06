// Fill out your copyright notice in the Description page of Project Settings.

#include "LMIntegration/Public/SemanticParser.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "NNE.h"
#include "Misc/FileHelper.h"

// Protecting third party includes
THIRD_PARTY_INCLUDES_START
#include "tokenizers_cpp.h"
THIRD_PARTY_INCLUDES_END

using namespace tokenizers;


bool USemanticParser::InitializeModel(UNNEModelData* InModelData)
{
	if (!InModelData) return false;
	
	// Get the ONNX CPU Runtime
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(FString("NNERuntimeORTCpu"));
	
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("NNE ONNX CPU Runtime is not valid."));
		return false;
	}
	
	// Create the Model and the Instance
	TSharedPtr<UE::NNE::IModelCPU> Model = Runtime -> CreateModelCPU(InModelData);
	if (Model.IsValid())
	{
		ModelInstance = Model -> CreateModelInstanceCPU();
		return ModelInstance.IsValid();
	}
	
	return false;
}

// float USemanticParser::CalculateCosineSimilarity(const TArray<float>& VectorA, const TArray<float>& VectorB)
// {
// 	if (VectorA.Num() != VectorB.Num() || VectorA.Num() == 0)
// 	{
// 		return 0.0f;
// 	}
// 	
// 	float DotProduct = 0.0f;
// 	float NormA = 0.0f;
// 	float NormB = 0.0f;
// 	
// 	for (int32 i=0; i<VectorA.Num(); ++i)
// 	{
// 		DotProduct += VectorA[i] * VectorB[i];
// 		NormA += VectorA[i] * VectorA[i];
// 		NormB += VectorB[i] * VectorB[i];
// 	}
// 	
// 	if (NormA == 0.0f || NormB == 0.0f)
// 	{
// 		return 0.0f;
// 	}
// 	
// 	// Return the score between 0.0 and 1.0
// 	return DotProduct / (FMath::Sqrt(NormA) + FMath::Sqrt(NormB));
// }

float USemanticParser::CalculateCosineSimilarity(const TArray<float>& VecA, const TArray<float>& VecB)
{
	// Safety check: Vectors must be the exact same size (384)
	if (VecA.Num() != VecB.Num() || VecA.IsEmpty()) return 0.0f;

	float DotProduct = 0.0f;
	float MagnitudeA = 0.0f;
	float MagnitudeB = 0.0f;

	// Calculate Dot Product and the squared magnitudes in one pass
	for (int32 i = 0; i < VecA.Num(); ++i)
	{
		DotProduct += VecA[i] * VecB[i];
		MagnitudeA += VecA[i] * VecA[i];
		MagnitudeB += VecB[i] * VecB[i];
	}

	// Safety check to prevent divide-by-zero crashes
	if (MagnitudeA == 0.0f || MagnitudeB == 0.0f) return 0.0f;

	// Final Cosine Similarity formula: DotProduct / (Sqrt(MagA) * Sqrt(MagB))
	return DotProduct / (FMath::Sqrt(MagnitudeA) * FMath::Sqrt(MagnitudeB));
}

TArray<float> USemanticParser::GetSemanticEmbedding(const TArray<int64>& InputIDs, const TArray<int64>& AttentionMask) const
{
    if (!ModelInstance.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("ModelInstance is not valid. Call InitializeModel first."));
        return TArray<float>();
    }

    int32 NumInputs = ModelInstance->GetInputTensorDescs().Num();

    TArray<int64> TokenTypeIDs;
    TokenTypeIDs.Init(0, InputIDs.Num());

    TArray<UE::NNE::FTensorBindingCPU> InputBindings;
    InputBindings.SetNumZeroed(NumInputs);

    TArray<UE::NNE::FTensorShape> InputShapes;
    InputShapes.SetNum(NumInputs);

    if (NumInputs > 0)
    {
        InputBindings[0].Data = (void*)InputIDs.GetData();
        InputBindings[0].SizeInBytes = InputIDs.Num() * sizeof(int64);
        InputShapes[0] = UE::NNE::FTensorShape::Make({ 1, (uint32)InputIDs.Num() });
    }

    if (NumInputs > 1)
    {
        InputBindings[1].Data = (void*)AttentionMask.GetData();
        InputBindings[1].SizeInBytes = AttentionMask.Num() * sizeof(int64);
        InputShapes[1] = UE::NNE::FTensorShape::Make({ 1, (uint32)AttentionMask.Num() });
    }

    if (NumInputs > 2)
    {
        InputBindings[2].Data = (void*)TokenTypeIDs.GetData();
        InputBindings[2].SizeInBytes = TokenTypeIDs.Num() * sizeof(int64);
        InputShapes[2] = UE::NNE::FTensorShape::Make({ 1, (uint32)TokenTypeIDs.Num() });
    }

    if (ModelInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to set input tensor shapes."));
        return TArray<float>();
    }

	// all-MiniLM-L6-v2 outputs exactly 384 dimensions per token.
	// So the total floats = (Number of Tokens) * 384
	uint64 TotalOutputFloats = InputIDs.Num() * 384;

    TArray<float> RawOutput;
    RawOutput.SetNumZeroed(TotalOutputFloats); 

    TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
    OutputBindings.SetNumZeroed(1);
    OutputBindings[0].Data = (void*)RawOutput.GetData();
    OutputBindings[0].SizeInBytes = RawOutput.Num() * sizeof(float);

    if (ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
    {
        TArray<float> FinalEmbedding;
        FinalEmbedding.SetNumZeroed(384);
        
        for(int32 i = 0; i < 384; i++)
        {
             if (i < RawOutput.Num()) 
             {
                 FinalEmbedding[i] = RawOutput[i];
             }
        }
        
        return FinalEmbedding;
    }

    UE_LOG(LogTemp, Error, TEXT("NNE RunSync failed."));
    return TArray<float>();
}

bool USemanticParser::InitializeTokenizer()
{
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
	auto Tokenizer = Tokenizer::FromBlobJSON(StdJsonBlob);
    
	if (Tokenizer)
	{
		TokenizerInstance = new std::unique_ptr<class Tokenizer>(std::move(Tokenizer));
		return true;
	}
    
	UE_LOG(LogTemp, Error, TEXT("Failed to parse Tokenizer JSON data."));
	return false;
}

// void USemanticParser::CacheCommandEmbeddings(const TArray<FString>& PredefinedCommands)
// {
//     CachedCommandsEmbeddings.Empty();
//
//     if (PredefinedCommands.IsEmpty())
//     {
//         UE_LOG(LogTemp, Error, TEXT("CACHE ERROR: The input array of predefined commands is empty!"));
//         return;
//     }
//
//     for (const FString& Command : PredefinedCommands)
//     {
//         TArray<int64> InputIDs;
//         TArray<int64> AttentionMask;
//     	
//         if (!TokenizeString(Command, InputIDs, AttentionMask))
//         {
//             UE_LOG(LogTemp, Error, TEXT("CACHE ERROR: TokenizeString failed for command: %s"), *Command);
//             continue;
//         }
//     	
//         TArray<float> Embedding = GetSemanticEmbedding(InputIDs, AttentionMask);
//         if (Embedding.IsEmpty())
//         {
//             UE_LOG(LogTemp, Error, TEXT("CACHE ERROR: GetSemanticEmbedding returned an empty array for: %s. Is ModelInstance valid?"), *Command);
//             continue;
//         }
//     	
//         if (Embedding.Num() != 384)
//         {
//             UE_LOG(LogTemp, Error, TEXT("CACHE ERROR: Model output size is %d, expected 384 for command: %s"), Embedding.Num(), *Command);
//             continue;
//         }
//
//         // Success!
//         CachedCommandsEmbeddings.Add(Command, Embedding);
//     }
//     
//     // Final report
//     if (CachedCommandsEmbeddings.IsEmpty())
//     {
//         UE_LOG(LogTemp, Error, TEXT("CACHE ERROR: Finished loop, but 0 commands were cached."));
//     }
//     else
//     {
//         UE_LOG(LogTemp, Log, TEXT("CACHE SUCCESS: Successfully cached %d command embeddings."), CachedCommandsEmbeddings.Num());
//     }
// }
void USemanticParser::CacheCommandEmbeddings(const TMap<FString, FAliasList>& CommandAliases)
{
	CachedAliasEmbeddings.Empty();
	AliasToCommandMap.Empty();

	if (CommandAliases.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("CACHE ERROR: The input dictionary is empty!"));
		return;
	}

	for (const auto& Pair : CommandAliases)
	{
		const FString& CommandID = Pair.Key;
		// Grab the array out of the struct!
		const TArray<FString>& Aliases = Pair.Value.Aliases; 

		for (const FString& Alias : Aliases)
		{
			TArray<int64> InputIDs;
			TArray<int64> AttentionMask;

			if (TokenizeString(Alias, InputIDs, AttentionMask))
			{
				TArray<float> Embedding = GetSemanticEmbedding(InputIDs, AttentionMask);
                
				if (Embedding.Num() == 384)
				{
					CachedAliasEmbeddings.Add(Alias, Embedding);
					AliasToCommandMap.Add(Alias, CommandID);
				}
			}
		}
	}
    
	UE_LOG(LogTemp, Warning, TEXT("CACHE SUCCESS: Loaded %d command aliases into memory."), CachedAliasEmbeddings.Num());
}

// FString USemanticParser::GetBestMatchingCommand(const FString& PlayerInput)
// {
// 	if (CachedCommandsEmbeddings.IsEmpty()) return TEXT("Error: Cache Empty");
//
// 	// Check what text the C++ is ACTUALLY receiving from the UI
// 	UE_LOG(LogTemp, Warning, TEXT("--- PARSING NEW INPUT: '%s' ---"), *PlayerInput);
//
// 	TArray<int64> InputIDs;
// 	TArray<int64> AttentionMask;
//
// 	if (!TokenizeString(PlayerInput, InputIDs, AttentionMask)) 
// 	{
// 		UE_LOG(LogTemp, Error, TEXT("MATCH ERROR: Tokenization Failed"));
// 		return TEXT("Error: Tokenization Failed");
// 	}
//
// 	TArray<float> PlayerEmbedding = GetSemanticEmbedding(InputIDs, AttentionMask);
// 	if (PlayerEmbedding.IsEmpty())
// 	{
// 		UE_LOG(LogTemp, Error, TEXT("MATCH ERROR: Player Embedding Failed. NNE returned empty."));
// 		return TEXT("Error: Embedding Failed");
// 	}
//
// 	FString BestCommand = TEXT("None");
// 	float HighestScore = -1.0f;
//
// 	for (const auto& CachedPair : CachedCommandsEmbeddings)
// 	{
// 		const FString& CommandName = CachedPair.Key;
// 		const TArray<float>& CommandVector = CachedPair.Value;
//
// 		float SimilarityScore = CalculateCosineSimilarity(PlayerEmbedding, CommandVector);
//
// 		// Reveal the hidden math for every single command
// 		UE_LOG(LogTemp, Log, TEXT("Comparing against '%s' -> Score: %f"), *CommandName, SimilarityScore);
//
// 		if (SimilarityScore > HighestScore)
// 		{
// 			HighestScore = SimilarityScore;
// 			BestCommand = CommandName;
// 		}
// 	}
// 	
// 	// Around 0.55 to 0.6 is considered to be industry standard
// 	float ConfidenceThreshold = 0.60f; 
//
// 	if (HighestScore < ConfidenceThreshold)
// 	{
// 		UE_LOG(LogTemp, Warning, TEXT("Score %f was too low. Returning None."), HighestScore);
// 		return TEXT("None");
// 	}
// 	
// 	// Announce the winner
// 	UE_LOG(LogTemp, Warning, TEXT("WINNER: %s (Score: %f)"), *BestCommand, HighestScore);
//     
// 	return BestCommand;
// }

FString USemanticParser::GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold)
{
	if (CachedAliasEmbeddings.IsEmpty()) return TEXT("Error: Cache Empty");

	TArray<int64> InputIDs;
	TArray<int64> AttentionMask;

	if (!TokenizeString(PlayerInput, InputIDs, AttentionMask)) return TEXT("Error: Tokenization Failed");

	TArray<float> PlayerEmbedding = GetSemanticEmbedding(InputIDs, AttentionMask);
	if (PlayerEmbedding.IsEmpty()) return TEXT("Error: Embedding Failed");

	FString BestAlias = TEXT("None");
	float HighestScore = -1.0f;

	// Compare player text against EVERY cached natural phrase
	for (const auto& CachedPair : CachedAliasEmbeddings)
	{
		const FString& AliasText = CachedPair.Key;
		const TArray<float>& AliasVector = CachedPair.Value;

		float SimilarityScore = CalculateCosineSimilarity(PlayerEmbedding, AliasVector);

		if (SimilarityScore > HighestScore)
		{
			HighestScore = SimilarityScore;
			BestAlias = AliasText;
		}
	}
	
	if (HighestScore < ConfidenceThreshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("Rejected! Best match was '%s' (Score: %f) but fell below threshold of %f"), *BestAlias, HighestScore, ConfidenceThreshold);
		return TEXT("None");
	}
	
	FString WinningCommand = AliasToCommandMap[BestAlias];
	UE_LOG(LogTemp, Warning, TEXT("WINNER: %s via alias '%s' (Score: %f)"), *WinningCommand, *BestAlias, HighestScore);
    
	return WinningCommand;
}

bool USemanticParser::TokenizeString(const FString& InputText, TArray<int64>& OutInputIDs,
                                     TArray<int64>& OutAttentionMask) const
{
	if (!TokenizerInstance) return false;
	
	// Cast the void pointer back to the actual library object
	auto* TokenizerPtr = static_cast<std::unique_ptr<class Tokenizer>*>(TokenizerInstance);
	
	std::string StdInput = TCHAR_TO_UTF8(*InputText);
	
	// Encode the string
	std::vector<int> RawTokens = (*TokenizerPtr) -> Encode(StdInput);
	
	const int32 MaxSequenceLength = 128; // Standard for all-MiniLM-L6-v2
	OutInputIDs.Init(0,MaxSequenceLength);
	OutAttentionMask.Init(0,MaxSequenceLength);
	
	// all-MiniLM-L6-v2 requires [CLS] (101) at the start and [SEP] (102) at the end
	OutInputIDs[0]=101;
	OutAttentionMask[0]=1;
	
	int32 CurrentIndex = 1;
	for (int i=0; i<RawTokens.size() && CurrentIndex<MaxSequenceLength - 1; ++i)
	{
		OutInputIDs[CurrentIndex] = static_cast<int64>(RawTokens[i]);
		OutAttentionMask[CurrentIndex] = 1;
		CurrentIndex++;
	}
	
	OutInputIDs[CurrentIndex] = 102;
	OutAttentionMask[CurrentIndex] = 1;
	
	return true;
}

FString USemanticParser::GetTokenizerFilePath()
{
	// FPaths::ProjectContentDir() dynamically finds the 'Content' folder in both the Editor and the shipped build
	FString TokenizerPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("NLP_DATA"), TEXT("tokenizer.json"));
	
	// Verify if the file exists before trying to load it
	if (!IFileManager::Get().FileExists(*TokenizerPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Tokenizer file not found: %s"), *TokenizerPath);
	}
	
	return TokenizerPath;
}
