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

	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(Tokens.Num()) })); 
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


	TArray<int64> Tokens64;
	Tokens64.SetNumUninitialized(Tokens.Num());
	for (int32 i = 0; i < Tokens.Num(); ++i) Tokens64[i] = static_cast<int64>(Tokens[i]);

	TArray<int64> PromptTokens64 = { 0 };
	TArray<int64> PromptLen64 = { 1 };
	TArray SpeedArray = { SpeechSpeed };


	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.SetNumZeroed(4);
	InputBindings[0] = { static_cast<void*>(Tokens64.GetData()), static_cast<uint32>(Tokens64.Num() * sizeof(int64)) };
	InputBindings[1] = { static_cast<void*>(PromptTokens64.GetData()), static_cast<uint32>(PromptTokens64.Num() * sizeof(int64)) };
	InputBindings[2] = { static_cast<void*>(PromptLen64.GetData()), static_cast<uint32>(PromptLen64.Num() * sizeof(int64)) };
	InputBindings[3] = { static_cast<void*>(SpeedArray.GetData()), static_cast<uint32>(SpeedArray.Num() * sizeof(float)) };

	TArrayView<const UE::NNE::FTensorDesc> OutputDescs = TextEncoderInstance->GetOutputTensorDescs();
	if (OutputDescs.IsEmpty()) return false;

	TArrayView<const int32> SymbolicShape = OutputDescs[0].GetShape().GetData();
	

	int32 FeatureDim = SymbolicShape.Num() == 3 ? SymbolicShape[2] : 100;
	if (FeatureDim <= 0) FeatureDim = 100;

	uint32 OutputVolume = 1 * Tokens.Num() * FeatureDim;
	OutTextFeatures.SetNumUninitialized(OutputVolume);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({ static_cast<void*>(OutTextFeatures.GetData()), static_cast<uint32>(OutputVolume * sizeof(float)) });


	if (TextEncoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok) return false;

	UE_LOG(LogTextToSpeech, Log, TEXT("Text Encoder executed successfully! Generated %d text features."), OutputVolume);
	return true;
}

bool FLuxTTSInference::RunFMDecoder(const TArray<float>& TextFeatures, TArray<float>& OutAcousticFeatures) const
{
	if (!FMDecoderInstance.IsValid()) return false;

	int32 TextDim = 100; // Synchronized with Text Encoder
	int32 AcousticDim = 100; 
	int32 NumTokens = TextFeatures.Num() / TextDim;

	int32 BaseSeqLen = NumTokens * 12;
	int32 AudioSeqLen = BaseSeqLen;
	if (AudioSeqLen % 16 != 0) AudioSeqLen += (16 - (AudioSeqLen % 16));

	// Nearest-neighbor upsampling text to match audio length
	TArray<float> UpsampledText;
	UpsampledText.SetNumUninitialized(AudioSeqLen * TextDim);
	for (int32 i = 0; i < AudioSeqLen; ++i) {
		int32 Idx = FMath::Clamp((i * NumTokens) / AudioSeqLen, 0, NumTokens - 1);
		for (int32 d = 0; d < TextDim; ++d) UpsampledText[i * TextDim + d] = TextFeatures[Idx * TextDim + d];
	}

	int32 PromptLen = AudioSeqLen;
	TArray<float> DummySpeech;
	DummySpeech.SetNumZeroed(PromptLen * AcousticDim); 

	// Initialize the starting canvas (x_0) with Standard Normal Gaussian Noise N(0,1)
	// We use the Box-Muller transform to get proper Gaussian distribution, not uniform!
	TArray<float> X_Canvas;
	X_Canvas.SetNumUninitialized(AudioSeqLen * AcousticDim);
	for (int32 i = 0; i < X_Canvas.Num(); i += 2) 
	{
		float u1 = FMath::Max(FMath::FRand(), FLT_MIN); 
		float u2 = FMath::FRand();
		float r = FMath::Sqrt(-2.0f * FMath::Loge(u1));
		float theta = 2.0f * PI * u2;
		X_Canvas[i] = r * FMath::Cos(theta);
		if (i + 1 < X_Canvas.Num()) {
			X_Canvas[i + 1] = r * FMath::Sin(theta);
		}
	}

	// THE ODE SOLVER LOOP (Euler Method) ---
	int32 NumSteps = 8; // 8 steps gives excellent human quality
	float dt = 1.0f / NumSteps;
	TArray<float> Guidance_Val = { 3.0f }; 
	
	TArray<float> VelocityOutput;
	VelocityOutput.SetNumUninitialized(AudioSeqLen * AcousticDim);

	// Define Shapes once outside the loop
	TArray<UE::NNE::FTensorShape> Shapes;
	TArray<uint32> EmptyShape; 
	Shapes.Add(UE::NNE::FTensorShape::Make(EmptyShape)); // t
	Shapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AudioSeqLen), static_cast<uint32>(AcousticDim) })); // x
	Shapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AudioSeqLen), static_cast<uint32>(TextDim) })); // text
	Shapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(PromptLen), static_cast<uint32>(AcousticDim) })); // speech
	Shapes.Add(UE::NNE::FTensorShape::Make(EmptyShape)); // guidance
	FMDecoderInstance->SetInputTensorShapes(Shapes);

	// Run the network multiple times to slowly denoise the audio
	for (int32 step = 0; step < NumSteps; ++step)
	{
		float CurrentT = static_cast<float>(step) / NumSteps + 0.001f; 
		if (CurrentT > 1.0f) CurrentT = 1.0f; // Clamp just to be safe
		TArray<float> T_Val = { CurrentT };

		TArray<UE::NNE::FTensorBindingCPU> In;
		In.Add({ static_cast<void*>(T_Val.GetData()), 4 });
		In.Add({ static_cast<void*>(X_Canvas.GetData()), static_cast<uint32>(X_Canvas.Num() * 4) });
		In.Add({ static_cast<void*>(UpsampledText.GetData()), static_cast<uint32>(UpsampledText.Num() * 4) });
		In.Add({ static_cast<void*>(DummySpeech.GetData()), static_cast<uint32>(DummySpeech.Num() * 4) });
		In.Add({ static_cast<void*>(Guidance_Val.GetData()), 4 });

		TArray<UE::NNE::FTensorBindingCPU> Out;
		Out.Add({ static_cast<void*>(VelocityOutput.GetData()), static_cast<uint32>(VelocityOutput.Num() * 4) });

		// Predict the Velocity
		if (FMDecoderInstance->RunSync(In, Out) != UE::NNE::EResultStatus::Ok) return false;

		// Euler Integration: X_new = X_old + (Velocity * dt)
		for (int32 i = 0; i < X_Canvas.Num(); ++i)
		{
			X_Canvas[i] += VelocityOutput[i] * dt;
		}
	}

	// The fully denoised canvas is our final Mel-Spectrogram!
	OutAcousticFeatures = X_Canvas;

	UE_LOG(LogTextToSpeech, Log, TEXT("FM Decoder ODE Loop completed! Generated %d acoustic features."), OutAcousticFeatures.Num());
	return true;
}

bool FLuxTTSInference::RunVocoder(const TArray<float>& AcousticFeatures, int32 AudioSeqLen, TArray<float>& OutAudioSamples) const
{
	if (!VocoderInstance.IsValid()) return false;

	int32 AcousticDim = 100;
	
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
	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AcousticDim), static_cast<uint32>(AudioSeqLen) }));

	if (VocoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to set shapes for Vocoder."));
		return false;
	}
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({ static_cast<void*>(TransposedMels.GetData()), static_cast<uint32>(TransposedMels.Num() * sizeof(float)) });
	
	uint32 OutputVolume = AudioSeqLen * 256; 
	OutAudioSamples.SetNumUninitialized(OutputVolume);

	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
	OutputBindings.Add({ static_cast<void*>(OutAudioSamples.GetData()), static_cast<uint32>(OutputVolume * sizeof(float)) });
	
	if (VocoderInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to execute Vocoder on the GPU."));
		return false;
	}

	UE_LOG(LogTextToSpeech, Log, TEXT("Vocoder executed successfully! Generated %d raw audio samples."), OutputVolume);
	return true;
}