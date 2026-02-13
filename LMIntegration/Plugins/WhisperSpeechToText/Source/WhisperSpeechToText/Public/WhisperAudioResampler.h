// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * Resamples the Audio to fit Whisper model's requirement of 16kHz and mono channel
 */

class WHISPERSPEECHTOTEXT_API WhisperAudioResampler
{
private:
	WhisperAudioResampler();
	~WhisperAudioResampler() {}; //Destructor does nothing
	
	// Making sure the resampler is unique
	WhisperAudioResampler(const WhisperAudioResampler& resampler) = delete; // Cannot be copied
	WhisperAudioResampler& operator=(const WhisperAudioResampler& resampler) = delete; // Cannot be assigned
	WhisperAudioResampler& operator=(WhisperAudioResampler&& resampler) = delete; // Cannot be moved
	
public:
	// int32 used to ensure large enough capacity
	static bool Resample(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 StartSampleRate, Audio::FAlignedFloatBuffer& OutAudio);
	static const double WHISPER_TARGET_RESAMPLE_RATE;
};
