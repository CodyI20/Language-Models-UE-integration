#include "WhisperSubsystem.h"
#include "WhisperSettings.h"
#include "AudioCaptureCore.h"
#include "JsonObjectConverter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"

void UWhisperSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bIsRecording = false;
	SampleRate = 16000; // Default, will update from device
	NumChannels = 1;
}

void UWhisperSubsystem::Deinitialize()
{
	if (bIsRecording)
	{
		StopAndTranscribe();
	}
	Super::Deinitialize();
}

void UWhisperSubsystem::StartRecording()
{
	if (bIsRecording)
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Already recording."));
		return;
	}

	{
		FScopeLock Lock(&BufferMutex);
		RecordingBuffer.Empty();
	}

	// Get device info
	FAudioCaptureDeviceInfo DeviceInfo;
	if (AudioCapture.GetAudioCaptureDeviceInfo(DeviceInfo))
	{
		SampleRate = DeviceInfo.SampleRate;
		NumChannels = DeviceInfo.NumInputChannels;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Whisper: Failed to get audio device info. Using defaults (16kHz, Mono)."));
		SampleRate = 16000;
		NumChannels = 1;
	}

	// Define the callback for audio data
	// Note: FAudioCapture usually provides data in float format (-1.0 to 1.0)
	auto OnCapture = [this](const void* AudioData, int32 NumFrames, int32 InNumChannels, int32 InSampleRate, double StreamTime, bool bOverflow)
	{
		const float* FloatData = static_cast<const float*>(AudioData);
		int32 NumSamples = NumFrames * InNumChannels;

		FScopeLock Lock(&BufferMutex);

		// Convert float to int16 PCM
		for (int32 i = 0; i < NumSamples; i++)
		{
			float Sample = FloatData[i];
			// Clamp to [-1.0, 1.0]
			Sample = FMath::Clamp(Sample, -1.0f, 1.0f);
			// Convert to int16
			int16 PCM = (int16)(Sample * 32767.0f);

			// Append to buffer (Little Endian)
			RecordingBuffer.Add((uint8)(PCM & 0xFF));
			RecordingBuffer.Add((uint8)((PCM >> 8) & 0xFF));
		}
	};

	// Open capture stream
	// We ask for the device's native sample rate/channels to avoid internal resampling issues if possible,
	// or we can try to force parameters if the API allows.

	if (AudioCapture.OpenAudioCaptureStream(OnCapture, NumChannels, SampleRate, 1024))
	{
		AudioCapture.StartStream();
		bIsRecording = true;
		UE_LOG(LogTemp, Log, TEXT("Whisper: Started recording at %d Hz, %d Channels."), SampleRate, NumChannels);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Whisper: Failed to open audio capture stream."));
		OnTranscriptionFailed.Broadcast(TEXT("Failed to open audio device."));
	}
}

void UWhisperSubsystem::StopAndTranscribe()
{
	if (!bIsRecording)
	{
		return;
	}

	AudioCapture.StopStream();
	AudioCapture.CloseStream();
	bIsRecording = false;

	// Copy buffer under lock
	TArray<uint8> LocalBuffer;
	{
		FScopeLock Lock(&BufferMutex);
		LocalBuffer = RecordingBuffer;
	}

	UE_LOG(LogTemp, Log, TEXT("Whisper: Stopped recording. Captured %d bytes."), LocalBuffer.Num());

	if (LocalBuffer.Num() == 0)
	{
		OnTranscriptionFailed.Broadcast(TEXT("No audio captured."));
		return;
	}

	// Prepare HTTP Request
	const UWhisperSettings* Settings = GetDefault<UWhisperSettings>();
	FString ApiUrl = Settings->ApiUrl;
	FString ApiKey = Settings->ApiKey;

	if (ApiUrl.IsEmpty())
	{
		OnTranscriptionFailed.Broadcast(TEXT("API URL is empty in Whisper Settings."));
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiUrl);
	Request->SetVerb(TEXT("POST"));

	// Create Multipart Form Data
	FString Boundary = TEXT("---------------------------") + FGuid::NewGuid().ToString();
	Request->SetHeader(TEXT("Content-Type"), TEXT("multipart/form-data; boundary=") + Boundary);

	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + ApiKey);
	}

	TArray<uint8> Payload;

	// 1. Add Model Field (often required)
	FString ModelField = TEXT("--") + Boundary + TEXT("\r\n") +
		TEXT("Content-Disposition: form-data; name=\"model\"\r\n\r\n") +
		TEXT("whisper-1") + TEXT("\r\n");

	// Convert string to bytes
	FTCHARToUTF8 ModelFieldUtf8(*ModelField);
	Payload.Append((uint8*)ModelFieldUtf8.Get(), ModelFieldUtf8.Length());

	// 2. Add File Field
	FString FileHeader = TEXT("--") + Boundary + TEXT("\r\n") +
		TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n") +
		TEXT("Content-Type: audio/wav\r\n\r\n");

	FTCHARToUTF8 FileHeaderUtf8(*FileHeader);
	Payload.Append((uint8*)FileHeaderUtf8.Get(), FileHeaderUtf8.Length());

	// 3. Add WAV Header + PCM Data
	TArray<uint8> WavHeader = GenerateWavHeader(LocalBuffer.Num());
	Payload.Append(WavHeader);
	Payload.Append(LocalBuffer);

	Payload.Append((uint8*)"\r\n", 2);

	// 4. Close Boundary
	FString EndBoundary = TEXT("--") + Boundary + TEXT("--\r\n");
	FTCHARToUTF8 EndBoundaryUtf8(*EndBoundary);
	Payload.Append((uint8*)EndBoundaryUtf8.Get(), EndBoundaryUtf8.Length());

	Request->SetContent(Payload);

	// Bind Callback
	Request->OnProcessRequestComplete().BindUObject(this, &UWhisperSubsystem::OnProcessRequestComplete);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Log, TEXT("Whisper: Request sent to %s"), *ApiUrl);
}

void UWhisperSubsystem::OnProcessRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	if (!bWasSuccessful || !Response.IsValid())
	{
		OnTranscriptionFailed.Broadcast(TEXT("Network Error: Request failed."));
		return;
	}

	if (Response->GetResponseCode() != 200)
	{
		FString ErrorMsg = FString::Printf(TEXT("API Error: %d - %s"), Response->GetResponseCode(), *Response->GetContentAsString());
		UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMsg);
		OnTranscriptionFailed.Broadcast(ErrorMsg);
		return;
	}

	// Parse JSON
	FString Content = Response->GetContentAsString();
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		if (JsonObject->HasField(TEXT("text")))
		{
			FString TranscribedText = JsonObject->GetStringField(TEXT("text"));
			OnTranscriptionComplete.Broadcast(TranscribedText);
		}
		else
		{
			// Check if there is an error field
			if (JsonObject->HasField(TEXT("error")))
			{
				FString Error = JsonObject->GetStringField(TEXT("error"));
				OnTranscriptionFailed.Broadcast(Error);
			}
			else
			{
				OnTranscriptionFailed.Broadcast(TEXT("Invalid JSON response: 'text' field missing."));
			}
		}
	}
	else
	{
		OnTranscriptionFailed.Broadcast(TEXT("Failed to parse JSON response."));
	}
}

TArray<uint8> UWhisperSubsystem::GenerateWavHeader(int32 DataSize) const
{
	TArray<uint8> Header;
	Header.SetNumUninitialized(44);

	// RIFF Chunk
	// 'RIFF'
	Header[0] = 'R'; Header[1] = 'I'; Header[2] = 'F'; Header[3] = 'F';

	int32 FileSize = 36 + DataSize;
	// File Size
	FMemory::Memcpy(Header.GetData() + 4, &FileSize, 4);

	// 'WAVE'
	Header[8] = 'W'; Header[9] = 'A'; Header[10] = 'V'; Header[11] = 'E';

	// fmt Chunk
	// 'fmt '
	Header[12] = 'f'; Header[13] = 'm'; Header[14] = 't'; Header[15] = ' ';

	int32 FmtChunkSize = 16;
	FMemory::Memcpy(Header.GetData() + 16, &FmtChunkSize, 4);

	int16 AudioFormat = 1; // PCM
	FMemory::Memcpy(Header.GetData() + 20, &AudioFormat, 2);

	int16 Channels = (int16)NumChannels;
	FMemory::Memcpy(Header.GetData() + 22, &Channels, 2);

	int32 SamplesPerSec = SampleRate;
	FMemory::Memcpy(Header.GetData() + 24, &SamplesPerSec, 4);

	int32 BytesPerSec = SampleRate * NumChannels * 2; // 16-bit = 2 bytes
	FMemory::Memcpy(Header.GetData() + 28, &BytesPerSec, 4);

	int16 BlockAlign = NumChannels * 2;
	FMemory::Memcpy(Header.GetData() + 32, &BlockAlign, 2);

	int16 BitsPerSample = 16;
	FMemory::Memcpy(Header.GetData() + 34, &BitsPerSample, 2);

	// data Chunk
	// 'data'
	Header[36] = 'd'; Header[37] = 'a'; Header[38] = 't'; Header[39] = 'a';

	FMemory::Memcpy(Header.GetData() + 40, &DataSize, 4);

	return Header;
}

void UWhisperSubsystem::OnAudioCaptureByte(const uint8* AudioData, int32 NumBytes)
{
	// Not used directly, we use lambda in StartRecording
}
