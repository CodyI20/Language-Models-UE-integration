#include "WhisperSettings.h"

UWhisperSettings::UWhisperSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Whisper");

	// Default to local Whisper server port
	ApiUrl = TEXT("http://127.0.0.1:8080/v1/audio/transcriptions");
	ApiKey = TEXT("");
}
