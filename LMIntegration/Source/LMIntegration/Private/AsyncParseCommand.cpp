// Fill out your copyright notice in the Description page of Project Settings.


#include "AsyncParseCommand.h"
#include "Async/Async.h"

UAsyncParseCommand* UAsyncParseCommand::AsyncGetBestMatchingCommand(UObject* WorldContenxtObject,
	USemanticParser* ParserSystem, const FString& PlayerInput, float ConfidenceThreshold)
{
	// Node creation + input storage
	UAsyncParseCommand* Node = NewObject<UAsyncParseCommand>();
	Node -> Parser = ParserSystem;
	Node -> InputText = PlayerInput;
	Node -> Threshold = ConfidenceThreshold;
	return Node;
}

void UAsyncParseCommand::Activate()
{
	if (!Parser)
	{
		OnFail.Broadcast(TEXT("None"));
		return;
	}
	
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
	{
		FString Result = Parser -> GetBestMatchingCommand(InputText, Threshold);
		
		AsyncTask(ENamedThreads::GameThread, [this, Result]()
		{
			if (Result == TEXT("None") || Result.StartsWith(TEXT("Error")))
			{
				OnFail.Broadcast(Result);
			}
			else
			{
				OnSuccess.Broadcast(Result);
			}
		});
	});
}
