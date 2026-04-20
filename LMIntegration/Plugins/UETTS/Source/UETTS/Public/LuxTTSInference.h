#pragma once

#include "CoreMinimal.h"
#include "NNERuntimeGPU.h"

// Forward declare the UNNEModelData class
class UNNEModelData;

class UETTS_API FLuxTTSInference
{
public:
	FLuxTTSInference();
	~FLuxTTSInference();

	bool InitializeModels();
	
	// Feeds the tokenized text to the AI and retrieves the features
	bool RunTextEncoder(const TArray<int32>& Tokens, float SpeechSpeed, TArray<float>& OutTextFeatures, const TArray<int32>* PromptTokens = nullptr) const;
	
	bool RunFMDecoder(const TArray<float>& TextFeatures, TArray<float>& OutAcousticFeatures, const TArray<float>* PromptAcousticFeatures = nullptr) const;
	
	bool RunVocoder(const TArray<float>& AcousticFeatures, int32 AudioSeqLen, TArray<float>& OutAudioSamples) const;

private:
	// Notice we changed FString FileName to FString AssetPath
	static bool LoadONNXModel(const FString& AssetPath, TSharedPtr<UE::NNE::IModelGPU>& OutModel, TSharedPtr<UE::NNE::IModelInstanceGPU>& OutModelInstance);

	TSharedPtr<UE::NNE::IModelGPU> TextEncoderModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> TextEncoderInstance;
	
	TSharedPtr<UE::NNE::IModelGPU> FMDecoderModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> FMDecoderInstance;
	
	TSharedPtr<UE::NNE::IModelGPU> VocoderModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> VocoderInstance;
};