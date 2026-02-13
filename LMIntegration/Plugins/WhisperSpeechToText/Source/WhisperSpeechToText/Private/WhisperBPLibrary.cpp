// Fill out your copyright notice in the Description page of Project Settings.


#include "WhisperBPLibrary.h"
#include "WhisperSpeechToText.h"
#include "WhisperAudioResampler.h"

FString UWhisperBPLibrary::TranscribeAudioBuffer(const TArray<float>& PCMData)
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("WhisperIntegration")))
	{
		return FWhisperSpeechToText::Get().TranscribeFromBuffer(PCMData.GetData(), PCMData.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UWhisperBPLibrary::TranscribeAudioBuffer - WhisperIntegration module is not loaded!"));
		return FString(TEXT("[Error: WhisperIntegration Module not loaded]"));
	}
}

FString UWhisperBPLibrary::TranslateMicToText(const UObject* WorldContextObject, USoundSubmix* SubmixToRecord)
{
	if (Audio::FMixerDevice* MixerDevice = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(WorldContextObject))
	{
		float sampleRate;
		float channelCount;
		
		// Resample to 16kHz, mono channel
		const Audio::FAlignedFloatBuffer& RecordedBuffer = MixerDevice -> StopRecording(SubmixToRecord, channelCount, sampleRate);
		Audio::FAlignedFloatBuffer ResampledBuffer = Audio::AlignedFloatBuffer();
		
		const bool resample_result = WhisperAudioResampler::Resample(RecordedBuffer.GetData(), RecordedBuffer.Num(), 
			(int32)channelCount, (int32)sampleRate, ResampledBuffer);
		
		if (!resample_result || 0 == ResampledBuffer.Num())
		{
			UE_LOG(LogTemp, Warning, TEXT("No audio data. Did you call Start Recording Output?"));
			return FString("");
		}
		
		RecordData.Reset(new Audio::FAudioRecordingData());
		RecordData->InputBuffer = Audio::TSampleBuffer<int16>(ResampledBuffer, 1, 16000);
		
#if !UE_BUILD_SHIPPING
		RecordData -> Writer.BeginWriteToWavFile(RecordData->InputBuffer, "record", "", [SubmixToRecord]()
		{
			if (SubmixToRecord && SubmixToRecord -> OnSubmixRecordedFileDone.IsBound())
			{
				SubmixToRecord -> OnSubmixRecordedFileDone.Broadcast(nullptr);
			}
			
			RecordData.Reset();
		}
);
#endif
		FString output = FWhisperSpeechToText::Get().TranscribeFromBuffer(ResampledBuffer.GetData(), ResampledBuffer.Num());
		
		return output;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Output recording is an audio mixer only feature."));
	}
	
	return FString("");
}
