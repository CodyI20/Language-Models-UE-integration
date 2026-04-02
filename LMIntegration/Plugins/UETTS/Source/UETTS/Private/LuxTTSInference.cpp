// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuxTTSInference.h"
#include "UETTS.h" 
#include "NNE.h" 
#include "NNEModelData.h"

FLuxTTSInference::FLuxTTSInference()
{
}

FLuxTTSInference::~FLuxTTSInference()
{
}

bool FLuxTTSInference::InitializeModels()
{
	UE_LOG(LogTextToSpeech, Log, TEXT("Initializing LuxTTS GPU Models..."));

	// Load Text Encoder
	FString TextEncoderPath = TEXT("/UETTS/text_encoder.text_encoder");
	bool bTextSuccess = LoadONNXModel(TextEncoderPath, TextEncoderModel, TextEncoderInstance);
	if (bTextSuccess)
	{
		UE_LOG(LogTextToSpeech, Log, TEXT("LuxTTS text_encoder GPU Asset loaded successfully!"));
	}

	// Load FM Decoder
	FString FMDecoderPath = TEXT("/UETTS/fm_decoder.fm_decoder");
	bool bFMSuccess = LoadONNXModel(FMDecoderPath, FMDecoderModel, FMDecoderInstance);
	if (bFMSuccess)
	{
		UE_LOG(LogTextToSpeech, Log, TEXT("LuxTTS fm_decoder GPU Asset loaded successfully!"));
	}
	
	FString VocoderPath = TEXT("/UETTS/vocoder.vocoder");
	bool bVocoderSuccess = LoadONNXModel(VocoderPath, VocoderModel, VocoderInstance);
	if (bVocoderSuccess)
	{
		UE_LOG(LogTextToSpeech, Log, TEXT("LuxTTS vocoder GPU Asset loaded successfully!"));
	}

	return bTextSuccess && bFMSuccess && bVocoderSuccess;
}

bool FLuxTTSInference::LoadONNXModel(const FString& AssetPath, TSharedPtr<UE::NNE::IModelGPU>& OutModel, TSharedPtr<UE::NNE::IModelInstanceGPU>& OutModelInstance)
{
	UNNEModelData* ModelDataAsset = Cast<UNNEModelData>(StaticLoadObject(UNNEModelData::StaticClass(), nullptr, *AssetPath));

	if (!ModelDataAsset)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to load ONNX Model Asset at: %s"), *AssetPath);
		return false;
	}

	TWeakInterfacePtr<INNERuntimeGPU> Runtime = UE::NNE::GetRuntime<INNERuntimeGPU>(FString("NNERuntimeORTDml"));
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Could not find the NNE GPU Runtime!"));
		return false;
	}

	OutModel = Runtime->CreateModelGPU(ModelDataAsset);
	if (OutModel.IsValid())
	{
		OutModelInstance = OutModel->CreateModelInstanceGPU();
		return OutModelInstance.IsValid();
	}

	return false;
}

bool FLuxTTSInference::RunTextEncoder(const TArray<int32>& Tokens, float SpeechSpeed, TArray<float>& OutTextFeatures) const
{
	if (!TextEncoderInstance.IsValid()) return false;

	TArrayView<const UE::NNE::FTensorDesc> InputDescs = TextEncoderInstance->GetInputTensorDescs();
	
	// --- 1. Define the Input Shapes ---
	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, (uint32)Tokens.Num() })); 
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, 1 })); 

	TArray<uint32> PromptLenShapeArgs;
	for (int32 i = 0; i < InputDescs[2].GetShape().Rank(); ++i) PromptLenShapeArgs.Add(1);
	InputShapes.Add(UE::NNE::FTensorShape::Make(PromptLenShapeArgs)); 

	TArray<uint32> SpeedShapeArgs;
	for (int32 i = 0; i < InputDescs[3].GetShape().Rank(); ++i) SpeedShapeArgs.Add(1);
	InputShapes.Add(UE::NNE::FTensorShape::Make(SpeedShapeArgs)); 

	if (TextEncoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to set shapes for Text Encoder."));
		return false;
	}

	// --- 2. Prepare Data Buffers ---
	TArray<int64> Tokens64;
	Tokens64.SetNumUninitialized(Tokens.Num());
	for (int32 i = 0; i < Tokens.Num(); ++i) Tokens64[i] = (int64)Tokens[i];

	TArray<int64> PromptTokens64 = { 0 };
	TArray<int64> PromptLen64 = { 1 };
	TArray<float> SpeedArray = { SpeechSpeed };

	// --- 3. Bind the Inputs ---
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.SetNumZeroed(4);
	InputBindings[0] = { (void*)Tokens64.GetData(), (uint32)(Tokens64.Num() * sizeof(int64)) };
	InputBindings[1] = { (void*)PromptTokens64.GetData(), (uint32)(PromptTokens64.Num() * sizeof(int64)) };
	InputBindings[2] = { (void*)PromptLen64.GetData(), (uint32)(PromptLen64.Num() * sizeof(int64)) };
	InputBindings[3] = { (void*)SpeedArray.GetData(), (uint32)(SpeedArray.Num() * sizeof(float)) };

	// --- 4. Prepare Output Bindings ---
	TArrayView<const UE::NNE::FTensorDesc> OutputDescs = TextEncoderInstance->GetOutputTensorDescs();
	if (OutputDescs.IsEmpty()) return false;

	TArrayView<const int32> SymbolicShape = OutputDescs[0].GetShape().GetData();
	
	// FIX: The true output feature dimension of the text encoder is 100!
	int32 FeatureDim = SymbolicShape.Num() == 3 ? SymbolicShape[2] : 100;
	if (FeatureDim <= 0) FeatureDim = 100;

	uint32 OutputVolume = 1 * Tokens.Num() * FeatureDim;
	OutTextFeatures.SetNumUninitialized(OutputVolume);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({ (void*)OutTextFeatures.GetData(), (uint32)(OutputVolume * sizeof(float)) });

	// --- 5. Run Inference ---
	if (TextEncoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok) return false;

	UE_LOG(LogTextToSpeech, Log, TEXT("Text Encoder executed successfully! Generated %d text features."), OutputVolume);
	return true;
}

bool FLuxTTSInference::RunFMDecoder(const TArray<float>& TextFeatures, TArray<float>& OutAcousticFeatures) const
{
	if (!FMDecoderInstance.IsValid()) return false;

	// --- 1. Deduce the Sequence Lengths & Dimensions ---
	int32 TextDim = 100; // Synchronized with Text Encoder
	int32 AcousticDim = 100; 
	int32 NumTokens = TextFeatures.Num() / TextDim;

	// Base sequence length for the audio (~12 frames per token)
	int32 BaseSeqLen = NumTokens * 12;
	
	// Pad to ensure UNet divisibility by 16
	int32 AudioSeqLen = BaseSeqLen;
	if (AudioSeqLen % 16 != 0)
	{
		AudioSeqLen += (16 - (AudioSeqLen % 16));
	}

	// --- 1.5. FIX: Stretch Text Features to match Audio Length! ---
	TArray<float> UpsampledText;
	UpsampledText.SetNumUninitialized(AudioSeqLen * TextDim);
	
	for (int32 i = 0; i < AudioSeqLen; ++i)
	{
		// Nearest-neighbor scaling to find which text token this audio frame belongs to
		int32 TokenIndex = (i * NumTokens) / AudioSeqLen; 
		TokenIndex = FMath::Clamp(TokenIndex, 0, NumTokens - 1);

		// Copy the 100 features for this token
		for (int32 d = 0; d < TextDim; ++d)
		{
			UpsampledText[i * TextDim + d] = TextFeatures[TokenIndex * TextDim + d];
		}
	}

	// --- 2. Prepare Data Buffers ---
	int32 PromptLen = AudioSeqLen; // Voice prompt must also be perfectly parallel!

	TArray<float> T_Val = { 0.5f }; 
	TArray<float> Guidance_Val = { 3.0f }; 

	// The latent canvas: Initialize with standard normal noise
	TArray<float> X_Latents;
	X_Latents.SetNumUninitialized(AudioSeqLen * AcousticDim);
	for (int32 i = 0; i < X_Latents.Num(); ++i) 
	{
		X_Latents[i] = FMath::RandRange(-1.0f, 1.0f);
	}

	// Dummy speech condition
	TArray<float> SpeechCond;
	SpeechCond.SetNumZeroed(PromptLen * AcousticDim); 

	// --- 3. Define Input Shapes ---
	TArray<UE::NNE::FTensorShape> InputShapes;
	TArray<uint32> EmptyShape; 
	
	InputShapes.Add(UE::NNE::FTensorShape::Make(EmptyShape)); // 0: t
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, (uint32)AudioSeqLen, (uint32)AcousticDim })); // 1: x
	
	// We bind the mathematically stretched shape here!
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, (uint32)AudioSeqLen, (uint32)TextDim })); // 2: text_condition
	
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, (uint32)PromptLen, (uint32)AcousticDim })); // 3: speech_condition
	InputShapes.Add(UE::NNE::FTensorShape::Make(EmptyShape)); // 4: guidance_scale

	if (FMDecoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to set shapes for FM Decoder."));
		return false;
	}

	// --- 4. Bind Inputs ---
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.SetNumZeroed(5);
	
	InputBindings[0] = { (void*)T_Val.GetData(), sizeof(float) };
	InputBindings[1] = { (void*)X_Latents.GetData(), (uint32)(X_Latents.Num() * sizeof(float)) };
	
	// Bind our newly stretched UpsampledText array instead of the raw TextFeatures
	InputBindings[2] = { (void*)UpsampledText.GetData(), (uint32)(UpsampledText.Num() * sizeof(float)) };
	
	InputBindings[3] = { (void*)SpeechCond.GetData(), (uint32)(SpeechCond.Num() * sizeof(float)) };
	InputBindings[4] = { (void*)Guidance_Val.GetData(), sizeof(float) };

	// --- 5. Prepare Output Bindings ---
	uint32 OutputVolume = 1 * AudioSeqLen * AcousticDim;
	OutAcousticFeatures.SetNumUninitialized(OutputVolume);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({ (void*)OutAcousticFeatures.GetData(), (uint32)(OutputVolume * sizeof(float)) });

	// --- 6. Run the Inference Test! ---
	if (FMDecoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to execute FM Decoder on the GPU."));
		return false;
	}

	UE_LOG(LogTextToSpeech, Log, TEXT("FM Decoder executed successfully! Generated %d acoustic features."), OutputVolume);
	return true;
}

bool FLuxTTSInference::RunVocoder(const TArray<float>& AcousticFeatures, int32 AudioSeqLen, TArray<float>& OutAudioSamples) const
{
	if (!VocoderInstance.IsValid()) return false;

	int32 AcousticDim = 100;

	// --- 1. Transpose the Data [Frames, 100] -> [100, Frames] ---
	TArray<float> TransposedMels;
	TransposedMels.SetNumUninitialized(AudioSeqLen * AcousticDim);
	
	for (int32 frame = 0; frame < AudioSeqLen; ++frame)
	{
		for (int32 dim = 0; dim < AcousticDim; ++dim)
		{
			// Flip the rows and columns
			TransposedMels[dim * AudioSeqLen + frame] = AcousticFeatures[frame * AcousticDim + dim];
		}
	}

	// --- 2. Define Input Shapes ---
	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, (uint32)AcousticDim, (uint32)AudioSeqLen }));

	if (VocoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to set shapes for Vocoder."));
		return false;
	}

	// --- 3. Bind Inputs ---
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({ (void*)TransposedMels.GetData(), (uint32)(TransposedMels.Num() * sizeof(float)) });

	// --- 4. Prepare Output Bindings ---
	// Vocos upsamples the frames into raw audio waves. 
	// At 24kHz, the hop length is exactly 256 audio samples per frame!
	uint32 OutputVolume = AudioSeqLen * 256; 
	OutAudioSamples.SetNumUninitialized(OutputVolume);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({ (void*)OutAudioSamples.GetData(), (uint32)(OutputVolume * sizeof(float)) });

	// --- 5. Run the Inference! ---
	if (VocoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to execute Vocoder on the GPU."));
		return false;
	}

	UE_LOG(LogTextToSpeech, Log, TEXT("Vocoder executed successfully! Generated %d raw audio samples."), OutputVolume);
	return true;
}