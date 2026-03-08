// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "SemanticParser.h"
#include "AsyncParseCommand.generated.h"

/**
 * 
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnParseCompleted, const FString&, BestCommand);

UCLASS()
class LMINTEGRATION_API UAsyncParseCommand : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	
public:
	// Output execution pins
	UPROPERTY(BlueprintAssignable)
	FOnParseCompleted OnSuccess;
	
	UPROPERTY(BlueprintAssignable)
	FOnParseCompleted OnFail;
	
	// Function which creates the node in the blueprint menu
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", WorldContext="WorldContextObject", Category="Semantic Parsing"))
	static UAsyncParseCommand* AsyncGetBestMatchingCommand(UObject* WorldContextObject, USemanticParser* ParserSystem, const FString& PlayerInput, float ConfidenceThreshold = 0.7f);
	
	virtual void Activate() override;
	
private:
	// Internal variable to hold data while the background thread works
	UPROPERTY()
	USemanticParser* Parser;
	
	FString InputText;
	float Threshold;
};
