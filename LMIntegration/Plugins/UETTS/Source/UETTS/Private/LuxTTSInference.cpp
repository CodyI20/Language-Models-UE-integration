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

bool FLuxTTSInference::RunTextEncoder(const TArray<int32>& Tokens, float SpeechSpeed, TArray<float>& OutTextFeatures, const TArray<int32>* PromptTokens) const
{
	if (!TextEncoderInstance.IsValid()) return false;

	TArrayView<const UE::NNE::FTensorDesc> InputDescs = TextEncoderInstance->GetInputTensorDescs();

	TArray<UE::NNE::FTensorShape> InputShapes;
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(Tokens.Num()) })); 
	// Always use 1 prompt token (zero token) regardless of input; reference voice conditioning causes NaN
	// The model was not trained with variable-length prompt conditioning
	const int32 PromptTokenCount = 1;
	InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(PromptTokenCount) })); 

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

	TArray<int64> PromptTokens64;
	PromptTokens64.SetNumUninitialized(PromptTokenCount);
	// Always use zero token; ignore PromptTokens input
	PromptTokens64[0] = 0;
	TArray<int64> PromptLen64 = { static_cast<int64>(PromptTokenCount) };
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

bool FLuxTTSInference::RunFMDecoder(const TArray<float>& TextFeatures, TArray<float>& OutAcousticFeatures, const TArray<float>* PromptAcousticFeatures) const
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

	const TArray<float>* PromptPtr = (PromptAcousticFeatures && PromptAcousticFeatures->Num() >= AcousticDim && (PromptAcousticFeatures->Num() % AcousticDim) == 0) ? PromptAcousticFeatures : nullptr;
	int32 PromptLen = AudioSeqLen;
	TArray<float> PromptSpeech;
	PromptSpeech.SetNumZeroed(PromptLen * AcousticDim);
	
	// Note: Reference voice acoustic features are currently not used in FM decoder due to model training mismatches.
	// The model expects zero-prompt conditioning. This preserves compatibility.
	if (PromptPtr && PromptPtr->Num() > 0)
	{
		UE_LOG(LogTextToSpeech, Log, TEXT("Reference voice features provided but not used (model trained with zero-prompt only)"));
	}
	else
	{
		UE_LOG(LogTextToSpeech, Log, TEXT("Using zero-filled prompt speech (%d frames)"), PromptLen);
	}

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

	// Integrate from noise -> data (t=1 -> t=0) for flow-matching decoding.
	int32 NumSteps = 8; // 8 steps keeps inference fast while improving intelligibility
	float dt = 1.0f / NumSteps;
	
	// Adaptive guidance: lower for large prompt inputs to avoid instability
	float GuidanceStrength = 3.0f;
	int32 PromptFrames = PromptPtr ? (PromptPtr->Num() / AcousticDim) : 0;
	if (PromptFrames > 400)
	{
		GuidanceStrength = 1.5f;
		UE_LOG(LogTextToSpeech, Warning, TEXT("Large prompt input (%d frames); reducing guidance from 3.0 to %.1f for stability"), PromptFrames, GuidanceStrength);
	}
	TArray<float> Guidance_Val = { GuidanceStrength }; 
	
	TArray<float> VelocityOutput;
	VelocityOutput.SetNumUninitialized(AudioSeqLen * AcousticDim);

	// Define Shapes once outside the loop
	TArray<UE::NNE::FTensorShape> Shapes;
	TArray<uint32> EmptyShape; 
	Shapes.Add(UE::NNE::FTensorShape::Make(EmptyShape)); // t
	Shapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AudioSeqLen), static_cast<uint32>(AcousticDim) })); // x
	Shapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AudioSeqLen), static_cast<uint32>(TextDim) })); // text
	Shapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AudioSeqLen), static_cast<uint32>(AcousticDim) })); // speech (always AudioSeqLen for concat compatibility)
	Shapes.Add(UE::NNE::FTensorShape::Make(EmptyShape)); // guidance
	FMDecoderInstance->SetInputTensorShapes(Shapes);

	// Run the network multiple times to slowly denoise the audio
	for (int32 step = 0; step < NumSteps; ++step)
	{
		// Sample in descending time order; avoid exact endpoints for stability.
		float CurrentT = 1.0f - ((static_cast<float>(step) + 0.5f) / NumSteps);
		CurrentT = FMath::Clamp(CurrentT, 0.001f, 0.999f);
		TArray<float> T_Val = { CurrentT };

		TArray<UE::NNE::FTensorBindingCPU> In;
		In.Add({ static_cast<void*>(T_Val.GetData()), 4 });
		In.Add({ static_cast<void*>(X_Canvas.GetData()), static_cast<uint32>(X_Canvas.Num() * 4) });
		In.Add({ static_cast<void*>(UpsampledText.GetData()), static_cast<uint32>(UpsampledText.Num() * 4) });
		In.Add({ static_cast<void*>(PromptSpeech.GetData()), static_cast<uint32>(PromptSpeech.Num() * 4) });
		In.Add({ static_cast<void*>(Guidance_Val.GetData()), 4 });

		TArray<UE::NNE::FTensorBindingCPU> Out;
		Out.Add({ static_cast<void*>(VelocityOutput.GetData()), static_cast<uint32>(VelocityOutput.Num() * 4) });

		// Predict the Velocity
		if (FMDecoderInstance->RunSync(In, Out) != UE::NNE::EResultStatus::Ok) return false;

		// Check for NaN explosion early
		int32 VelNaNs = 0;
		float VelMax = 0.0f;
		for (float v : VelocityOutput)
		{
			if (FMath::IsNaN(v)) VelNaNs++;
			else if (FMath::IsFinite(v)) VelMax = FMath::Max(VelMax, FMath::Abs(v));
		}
		if (VelNaNs > 0)
		{
			UE_LOG(LogTextToSpeech, Error, TEXT("FM Decoder ODE step %d: velocity output contains %d NaNs! GuidanceStrength=%f"), step, VelNaNs, GuidanceStrength);
			return false;
		}
		if (VelMax > 100.0f)
		{
			UE_LOG(LogTextToSpeech, Warning, TEXT("FM Decoder ODE step %d: velocity max %.1f (diverging?), clamping for stability"), step, VelMax);
			for (float& v : VelocityOutput)
			{
				v = FMath::Clamp(v, -10.0f, 10.0f);
			}
		}

		// Backward-time Euler integration: X_{t-dt} = X_t - v(x_t, t) * dt
		for (int32 i = 0; i < X_Canvas.Num(); ++i)
		{
			X_Canvas[i] -= VelocityOutput[i] * dt;
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

	bool bExpectChannelsFirst = true;
	TArrayView<const UE::NNE::FTensorDesc> VocoderInputDescs = VocoderInstance->GetInputTensorDescs();
	if (!VocoderInputDescs.IsEmpty())
	{
		const TArrayView<const int32> InputShape = VocoderInputDescs[0].GetShape().GetData();
		if (InputShape.Num() == 3)
		{
			if (InputShape[1] == AcousticDim && InputShape[2] != AcousticDim)
			{
				bExpectChannelsFirst = true;
			}
			else if (InputShape[2] == AcousticDim && InputShape[1] != AcousticDim)
			{
				bExpectChannelsFirst = false;
			}
		}
	}
	UE_LOG(LogTextToSpeech, Log, TEXT("Vocoder layout mode: %s"), bExpectChannelsFirst ? TEXT("[B,C,T] (transpose)") : TEXT("[B,T,C] (no transpose)"));

	TArray<float> VocoderInputBuffer;
	const float* VocoderInputPtr = AcousticFeatures.GetData();
	if (bExpectChannelsFirst)
	{
		VocoderInputBuffer.SetNumUninitialized(AudioSeqLen * AcousticDim);
		for (int32 frame = 0; frame < AudioSeqLen; ++frame)
		{
			for (int32 dim = 0; dim < AcousticDim; ++dim)
			{
				VocoderInputBuffer[dim * AudioSeqLen + frame] = AcousticFeatures[frame * AcousticDim + dim];
			}
		}
		VocoderInputPtr = VocoderInputBuffer.GetData();
	}

	TArray<UE::NNE::FTensorShape> InputShapes;
	if (bExpectChannelsFirst)
	{
		InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AcousticDim), static_cast<uint32>(AudioSeqLen) }));
	}
	else
	{
		InputShapes.Add(UE::NNE::FTensorShape::Make({ 1, static_cast<uint32>(AudioSeqLen), static_cast<uint32>(AcousticDim) }));
	}

	if (VocoderInstance->SetInputTensorShapes(InputShapes) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to set shapes for Vocoder."));
		return false;
	}
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	InputBindings.Add({ static_cast<void*>(const_cast<float*>(VocoderInputPtr)), static_cast<uint32>(AudioSeqLen * AcousticDim * sizeof(float)) });
	
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