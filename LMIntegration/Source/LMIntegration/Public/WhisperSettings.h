#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "WhisperSettings.generated.h"

/**
 * Settings for the Whisper STT Integration.
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Whisper Settings"))
class LMINTEGRATION_API UWhisperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWhisperSettings();

	/** The URL for the Whisper API endpoint (e.g. http://127.0.0.1:8080/v1/audio/transcriptions) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Whisper")
	FString ApiUrl;

	/** Optional API Key for authentication (e.g. Bearer token) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Whisper")
	FString ApiKey;
};
