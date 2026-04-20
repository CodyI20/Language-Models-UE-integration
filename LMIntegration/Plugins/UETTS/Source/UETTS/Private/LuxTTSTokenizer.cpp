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

	if (const int32* Found = VocabMap.Find(TEXT("^"))) BosTokenId = *Found;
	if (const int32* Found = VocabMap.Find(TEXT("$"))) EosTokenId = *Found;
	if (const int32* Found = VocabMap.Find(TEXT(" "))) SpaceTokenId = *Found;
	if (const int32* Found = VocabMap.Find(TEXT("_"))) UnknownTokenId = *Found;

	UE_LOG(LogTextToSpeech, Log, TEXT("Successfully loaded %d tokens into vocabulary."), VocabMap.Num());
	return true;
}

TArray<int32> FLuxTTSTokenizer::TokenizeText(const FString& InputText)
{
	TArray<int32> Tokens;
	FString LowerText = InputText.ToLower();
	Tokens.Reserve(LowerText.Len() + 2);

	if (BosTokenId != INDEX_NONE)
	{
		Tokens.Add(BosTokenId);
	}

	// Character-level mapping with explicit BOS/EOS and UNK handling.
	for (int32 i = 0; i < LowerText.Len(); ++i)
	{
		FString CharStr = LowerText.Mid(i, 1);

		if (int32* FoundId = VocabMap.Find(CharStr))
		{
			Tokens.Add(*FoundId);
		}
		else
		{
			if (CharStr == TEXT("\n") || CharStr == TEXT("\r") || CharStr == TEXT("\t"))
			{
				Tokens.Add(SpaceTokenId != INDEX_NONE ? SpaceTokenId : 3);
			}
			else
			{
				Tokens.Add(UnknownTokenId != INDEX_NONE ? UnknownTokenId : (SpaceTokenId != INDEX_NONE ? SpaceTokenId : 3));
			}
		}
	}

	if (EosTokenId != INDEX_NONE)
	{
		Tokens.Add(EosTokenId);
	}

	return Tokens;
}