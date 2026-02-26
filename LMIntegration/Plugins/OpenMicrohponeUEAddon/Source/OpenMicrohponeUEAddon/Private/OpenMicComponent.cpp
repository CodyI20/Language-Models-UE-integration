#include "OpenMicComponent.h"
#include "Voice.h"
#include "TimerManager.h"

UOpenMicComponent::UOpenMicComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOpenMicComponent::StartMicrophone()
{
	// 1. Check if the VoiceCapture already exists first!...
	if (VoiceCapture.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceCapture already exists!"));
		return;
	}
	
	//... then create it
	VoiceCapture = FVoiceModule::Get().CreateVoiceCapture(""); // Leaving the string empty ensures that the default microphone is used
	
	// In case there is no microphone, it would return a invalid pointer, thus causing the application to crash
	// To prevent this we can add a safety check
	if (!VoiceCapture.IsValid() || !VoiceCapture->Start())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create or start voice capture!"))
		return;
	}
	
	// 2. Start it
	VoiceCapture->Start();
	
	// 3. Set the timer to call CaptureAudioTick every 100 ms.
	GetWorld()->GetTimerManager().SetTimer(
		AudioCaptureTimerHandle,
		this,
		&UOpenMicComponent::CaptureAudioTick,
		0.1f, //100 ms
		true);
}

void UOpenMicComponent::StopMicrophone()
{
	// 1. Stop the timer
	GetWorld()->GetTimerManager().ClearTimer(AudioCaptureTimerHandle);
	
	// 2. Stop microphone capture
	if (VoiceCapture.IsValid())
	{
		VoiceCapture->Stop();
		VoiceCapture = nullptr;
	}
}

void UOpenMicComponent::CaptureAudioTick()
{
	// 1. Verify the validity of VoiceCapture
	if (!VoiceCapture.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("No voice capture!"));
		return;
	}
	
	// 2. Check the capture state and how much raw byte date is available
	
	uint32 AvailableVoiceData = 0;
	EVoiceCaptureState::Type CaptureState = VoiceCapture -> GetCaptureState(AvailableVoiceData);
	
	// 3. If data is available, read it into a TArray<uint8>
	TArray<float> FloatSamples;
	if (CaptureState == EVoiceCaptureState::Ok && AvailableVoiceData > 0)
	{
		TArray<uint8> RawVoiceData;
		RawVoiceData.SetNumUninitialized(AvailableVoiceData);
		
		uint32 OutAvailableVoiceData = 0;
		VoiceCapture -> GetVoiceData(RawVoiceData.GetData(), AvailableVoiceData, OutAvailableVoiceData);
		UE_LOG(LogTemp, Warning, TEXT("Captured %d bytes of audio from mic."), OutAvailableVoiceData);
		
		// 4. Convert the raw 16-bit PCM byte data into normalized float samples
		int32 NumSamples = OutAvailableVoiceData / 2;
		FloatSamples.SetNumUninitialized(NumSamples);
		
		const int16* SamplePtr = reinterpret_cast<const int16*>(RawVoiceData.GetData());
		for (int32 i = 0; i< NumSamples; ++i)
		{
			// Normalize between -1.0 and 1.0
			FloatSamples[i] = static_cast<float>(SamplePtr[i]) / 32768.0f;
		}
	}
	
	// 5. Send off the data using the delegate only if there is data to be sent
	if (FloatSamples.Num() > 0)
		OnAudioCaptured.Broadcast(FloatSamples);
}


