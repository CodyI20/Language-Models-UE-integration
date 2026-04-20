// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UETTS_API FLuxTTSTokenizer
{
public:
	FLuxTTSTokenizer();
	~FLuxTTSTokenizer();

	// Loads the tokens.txt file into the map
	bool LoadVocabulary();

	// Converts a string into an array of token IDs
	TArray<int32> TokenizeText(const FString& InputText);

private:
	// A map to store the Token-to-ID relationship
	TMap<FString, int32> VocabMap;

	int32 BosTokenId = INDEX_NONE;
	int32 EosTokenId = INDEX_NONE;
	int32 SpaceTokenId = INDEX_NONE;
	int32 UnknownTokenId = INDEX_NONE;
};