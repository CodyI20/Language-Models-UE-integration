// Fill out your copyright notice in the Description page of Project Settings.


#include "WhisperAudioResampler.h"

const double WhisperAudioResampler::WHISPER_TARGET_RESAMPLE_RATE = 16000.0F;

bool WhisperAudioResampler::Resample(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 StartSampleRate, Audio::FAlignedFloatBuffer& OutAudio)
{
	if (0 == NumFrames)
	{
		UE_LOG(LogTemp, Error, TEXT("No frames found, cannot resample."));
		return false;
	}
	
	// Interpolate to Mono-Channel
	Audio::FAlignedFloatBuffer MonoChannelAudio = Audio::FAlignedFloatBuffer();
	MonoChannelAudio.SetNum((NumFrames/NumChannels));
	
	for (int32 sampleCount = 0; sampleCount < NumFrames; sampleCount += NumChannels)
	{
		float sum = 0.f;
		for (int32 channelCount = 0; channelCount < NumChannels; ++channelCount)
		{
			sum += InAudio[sampleCount + channelCount];
		}
		MonoChannelAudio.Add(sum / NumChannels);
	}
	
	// Interpolate to target sample rate
	const double Ratio = double(StartSampleRate) / WHISPER_TARGET_RESAMPLE_RATE;
	const int32 NewLen = FMath::CeilToInt(MonoChannelAudio.Num() / Ratio);
	
	OutAudio.SetNum(NewLen);
	
	for (int32 i = 0; i < NewLen; ++i)
	{
		double sourceIndex = i * Ratio;
		int32 index = FMath::FloorToInt(sourceIndex);
		double frac = sourceIndex - index;
		
		float SampleValueAtStart = MonoChannelAudio[FMath::Clamp(index, 0, MonoChannelAudio.Num() - 1)];
		float SampleValueAtEnd = MonoChannelAudio[FMath::Clamp(index+1, 0, MonoChannelAudio.Num() - 1)];
		OutAudio[i] = (SampleValueAtStart + (SampleValueAtEnd - SampleValueAtStart) * frac);
	}
	return true;
}