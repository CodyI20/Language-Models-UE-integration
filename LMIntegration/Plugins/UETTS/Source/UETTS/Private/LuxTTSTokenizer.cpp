// Copyright Epic Games, Inc. All Rights Reserved.

#include "LuxTTSTokenizer.h"
#include "UETTS.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

FLuxTTSTokenizer::FLuxTTSTokenizer()
{
}

FLuxTTSTokenizer::~FLuxTTSTokenizer()
{
}

bool FLuxTTSTokenizer::LoadVocabulary()
{
	// Find the Plugin's Content Directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UETTS"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Could not find the UETTS plugin!"));
		return false;
	}

	FString ContentDir = Plugin->GetContentDir();
	FString TokensFilePath = FPaths::Combine(ContentDir, TEXT("tokens.txt"));

	// Load the file into an array of strings (one per line)
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *TokensFilePath))
	{
		UE_LOG(LogTextToSpeech, Error, TEXT("Failed to load tokens file at: %s"), *TokensFilePath);
		return false;
	}

	// Parse each line by splitting at the Tab (\t) character
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty()) continue;

		FString Token;
		FString IdStr;
		
		if (Line.Split(TEXT("\t"), &Token, &IdStr))
		{
			VocabMap.Add(Token, FCString::Atoi(*IdStr));
		}
	}

	UE_LOG(LogTextToSpeech, Log, TEXT("Successfully loaded %d tokens into vocabulary."), VocabMap.Num());
	return true;
}

TArray<int32> FLuxTTSTokenizer::TokenizeText(const FString& InputText)
{
	TArray<int32> Tokens;
	FString LowerText = InputText.ToLower();

	// Basic Character-level mapping for our first iteration
	for (int32 i = 0; i < LowerText.Len(); ++i)
	{
		FString CharStr = LowerText.Mid(i, 1);

		if (int32* FoundId = VocabMap.Find(CharStr))
		{
			Tokens.Add(*FoundId);
		}
		else
		{
			// If no recognized character, use the space token (ID 3 based on the txt file)
			Tokens.Add(3); 
		}
	}

	return Tokens;
}