// Fill out your copyright notice in the Description page of Project Settings.


#include "AsyncParseCommand.h"
#include "Async/Async.h"

UAsyncParseCommand* UAsyncParseCommand::AsyncGetBestMatchingCommand(USemanticParser* ParserSystem, const FString& PlayerInput, float ConfidenceThreshold)
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
		UE_LOG(LogTemp, Error, TEXT("Error: Parser not found!"));
		OnFail.Broadcast(ENPCAnimationID::ACTION_NONE);
		return;
	}
	
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
	{
		ENPCAnimationID Result = Parser -> GetBestMatchingCommand(InputText, Threshold);
		
		AsyncTask(ENamedThreads::GameThread, [this, Result]()
		{
			if (Result == ENPCAnimationID::ACTION_NONE)
			{
				OnFail.Broadcast(Result);
			}
			else
			{
				OnSuccess.Broadcast(Result);
			}
			
			SetReadyToDestroy();
		});
	});
}
