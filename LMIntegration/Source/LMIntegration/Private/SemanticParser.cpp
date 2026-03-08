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
	
	if (MagnitudeA == 0.0f || MagnitudeB == 0.0f) return 0.0f;
	
	return DotProduct / (FMath::Sqrt(MagnitudeA) * FMath::Sqrt(MagnitudeB));
}

TArray<float> USemanticParser::GetSemanticEmbedding(const TArray<int64>& InputIDs, const TArray<int64>& AttentionMask) const
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
            InputBindings[i].Data = (void*)CleanInputIDs.GetData();
            InputBindings[i].SizeInBytes = CleanInputIDs.Num() * sizeof(int64);
            InputShapes[i] = UE::NNE::FTensorShape::Make({ 1, (uint32)CleanInputIDs.Num() });
        }
        else if (InputName.Contains(TEXT("attention_mask")))
        {
            InputBindings[i].Data = (void*)CleanAttentionMask.GetData();
            InputBindings[i].SizeInBytes = CleanAttentionMask.Num() * sizeof(int64);
            InputShapes[i] = UE::NNE::FTensorShape::Make({ 1, (uint32)CleanAttentionMask.Num() });
        }
        else if (InputName.Contains(TEXT("token_type_ids")))
        {
            InputBindings[i].Data = (void*)TokenTypeIDs.GetData();
            InputBindings[i].SizeInBytes = TokenTypeIDs.Num() * sizeof(int64);
            InputShapes[i] = UE::NNE::FTensorShape::Make({ 1, (uint32)TokenTypeIDs.Num() });
        }
    }

    if (ModelInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok) return TArray<float>();

    // Math is now based ONLY on the real words
    uint64 TotalOutputFloats = CleanInputIDs.Num() * 384;
    
    TArray<float> RawOutput;
    RawOutput.SetNumZeroed(TotalOutputFloats);

    TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
    OutputBindings.SetNumZeroed(1);
    OutputBindings[0].Data = (void*)RawOutput.GetData();
    OutputBindings[0].SizeInBytes = RawOutput.Num() * sizeof(float);

    if (ModelInstance->RunSync(InputBindings, OutputBindings) == UE::NNE::EResultStatus::Ok)
    {
        int32 SeqLen = CleanInputIDs.Num();
        TArray<float> FinalEmbedding;
        FinalEmbedding.SetNumZeroed(384);

        int32 ValidTokens = 0;

        // Mean Pooling: Skip CLS (index 0) and SEP (index SeqLen - 1)
        if (SeqLen > 2)
        {
            for (int32 TokenIdx = 1; TokenIdx < SeqLen - 1; ++TokenIdx)
            {
                ValidTokens++;
                for (int32 Dim = 0; Dim < 384; ++Dim)
                {
                    int32 RawIndex = (TokenIdx * 384) + Dim;
                    if (RawIndex < RawOutput.Num()) FinalEmbedding[Dim] += RawOutput[RawIndex];
                }
            }
        }
        else // Fallback if the array only has 1 or 2 tokens for some reason
        {
            for (int32 TokenIdx = 0; TokenIdx < SeqLen; ++TokenIdx)
            {
                ValidTokens++;
                for (int32 Dim = 0; Dim < 384; ++Dim)
                {
                    int32 RawIndex = (TokenIdx * 384) + Dim;
                    if (RawIndex < RawOutput.Num()) FinalEmbedding[Dim] += RawOutput[RawIndex];
                }
            }
        }

        if (ValidTokens > 0)
        {
            for (int32 Dim = 0; Dim < 384; ++Dim) FinalEmbedding[Dim] /= (float)ValidTokens;
        }

        return FinalEmbedding;
    }

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

FString USemanticParser::GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold)
{
	if (CachedAliasEmbeddings.IsEmpty()) return TEXT("Error: Cache Empty");

	TArray<int64> InputIDs;
	TArray<int64> AttentionMask;

	if (!TokenizeString(PlayerInput, InputIDs, AttentionMask)) return TEXT("Error: Tokenization Failed");
	
	FString TokenString = TEXT("");
	
	for (int64 TokenID : InputIDs)
	{
		TokenString += FString::Printf(TEXT("%lld "), TokenID);
	}
	
	UE_LOG(LogTemp, Warning, TEXT("Raw tokens for '%s': [ %s]"), *PlayerInput, *TokenString);

	TArray<float> PlayerEmbedding = GetSemanticEmbedding(InputIDs, AttentionMask);
	if (PlayerEmbedding.IsEmpty()) return TEXT("Error: Embedding Failed");

	FString BestAlias = TEXT("None");
	float HighestScore = -1.0f;

	// Compare the input text against every cached phrase
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

void USemanticParser::CacheEmbeddingsFromDataTable(UDataTable* CommandTable)
{
	if (!CommandTable)
	{
		UE_LOG(LogTemp, Error, TEXT("Data Table is missing or is invalid!"));
		return;
	}
	
	CachedAliasEmbeddings.Empty();
	AliasToCommandMap.Empty();
	
	TArray<FCommandAliasRow*> AllRows;
	CommandTable->GetAllRows<FCommandAliasRow>(TEXT("SemanticParserCache"), AllRows);
	
	TArray<FName> RowNames = CommandTable->GetRowNames();
	
	for (int32 i=0; i<AllRows.Num(); ++i)
	{
		FString CommandID = RowNames[i].ToString();
		const TArray<FString>& Aliases = AllRows[i] -> Aliases;
		
		for (const FString& Alias : Aliases)
		{
			TArray<int64> InputIDs;
			TArray<int64> AttentionMask;
			
			if (TokenizeString(Alias, InputIDs, AttentionMask))
			{
				TArray<float> Embedding = GetSemanticEmbedding(InputIDs, AttentionMask);
				
				if (!Embedding.IsEmpty())
				{
					CachedAliasEmbeddings.Add(Alias, Embedding);
					AliasToCommandMap.Add(Alias, CommandID);
				}
			}
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("Data table SUCCESS: Loaded %d aliases into memory from Data Table."), CachedAliasEmbeddings.Num());
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
