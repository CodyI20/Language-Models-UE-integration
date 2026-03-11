// Fill out your copyright notice in the Description page of Project Settings.

#include "SemanticParser.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "NNE.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"

// Protecting third party includes
THIRD_PARTY_INCLUDES_START
#include "tokenizers_cpp.h"
THIRD_PARTY_INCLUDES_END

// Unnamed (Anonymous) namespace to prevent linkage errors in case any other .cpp file in the project ever uses the exact same variable name
namespace
{
	constexpr int32 EmbeddingDimension = 384;
}

using namespace tokenizers;

void USemanticParser::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InitializeTokenizer();
	InitializeModel();
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

ENPCAnimationID USemanticParser::GetBestMatchingCommand(const FString& PlayerInput, float ConfidenceThreshold)
{
	// Lock the function until the thread is done
	FScopeLock Lock(&InferenceMutex);
	
	if (CachedAliasEmbeddings.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Error: Cache is empty!"));
		return ENPCAnimationID::ACTION_NONE;
	};

	TArray<int64> InputIDs;
	
	if (!TokenizeString(PlayerInput, InputIDs))
	{
		UE_LOG(LogTemp, Error, TEXT("Error: Tokenization Failed"));
		return ENPCAnimationID::ACTION_NONE;
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
		return ENPCAnimationID::ACTION_NONE;
	};

	ENPCAnimationID WinningCommand = ENPCAnimationID::ACTION_NONE;
	FString BestAlias = TEXT("None");
	float Score = -1.0f;

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

		if (SimilarityScore > Score)
		{
			Score = SimilarityScore;
			BestAlias = AliasText;
			if (ENPCAnimationID* FoundCommand = AliasToCommandMap.Find(AliasText))
			{
				WinningCommand = *FoundCommand;
			}
		}
	}
	
	if (Score < ConfidenceThreshold)
	{
		UE_LOG(LogTemp, Warning, TEXT("Rejected! Best match was '%s' (Score: %f) but fell below threshold of %f"), *BestAlias, Score, ConfidenceThreshold);
		return ENPCAnimationID::ACTION_NONE;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("WINNER: %s via alias '%s' (Score: %f)"), *UEnum::GetValueAsString(WinningCommand), *BestAlias, Score);
    
	return WinningCommand;
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
		UKismetSystemLibrary::PrintString(this, TEXT("Data table is missing! Please provide one in the parsing system actor component!\n"
											   "Note that the system won't work without one"),
			true, false, FLinearColor::Red, 100.f, NAME_Error);
		return;
	}
	
	CachedAliasEmbeddings.Reset();
	AliasToCommandMap.Reset();
	
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
				TArray<float> Embedding = GetSemanticEmbedding(InputIDs);
				
				if (!Embedding.IsEmpty())
				{
					CachedAliasEmbeddings.Add(Alias, Embedding);
					AliasToCommandMap.Add(Alias, CommandID);
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

