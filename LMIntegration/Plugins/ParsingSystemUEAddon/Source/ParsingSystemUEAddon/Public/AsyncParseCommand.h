#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "SemanticParser.h"
#include "AsyncParseCommand.generated.h"

/**
 * Blueprint delegate fired when semantic parsing completes successfully or fails.
 * The output command is the best matching NPC animation ID chosen by the parser.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnParseCompleted, const ENPCAnimationID&, BestCommand);

/**
 * Async Blueprint node that queries a `USemanticParser` for the best matching command.
 *
 * This class is designed to be spawned from Blueprints and executed asynchronously,
 * allowing semantic parsing work to happen without blocking the game thread.
 */
UCLASS()
class PARSINGSYSTEMUEADDON_API UAsyncParseCommand : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	
public:
	/**
	 * Called when the parser returns a confident match.
	 *
	 * The `BestCommand` value contains the parser's selected NPC animation ID.
	 */
	UPROPERTY(BlueprintAssignable)
	FOnParseCompleted OnSuccess;
	
	/**
	 * Called when parsing fails or no result meets the confidence threshold.
	 *
	 * The `BestCommand` value can be used as a fallback or ignored by the Blueprint caller.
	 */
	UPROPERTY(BlueprintAssignable)
	FOnParseCompleted OnFail;
	
	/**
	 * Creates the async Blueprint node used to request the best matching command.
	 *
	 * @param ParserSystem The semantic parser instance that will process the input.
	 * @param PlayerInput The raw text input from the player.
	 * @param ConfidenceThreshold Minimum score required for a result to be considered successful.
	 * @return A configured async action object ready to be activated by Blueprint.
	 */
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", Category="Semantic Parsing"))
	static UAsyncParseCommand* AsyncGetBestMatchingCommand(USemanticParser* ParserSystem, const FString& PlayerInput, float ConfidenceThreshold = 0.7f);
	
	/**
	 * Starts the async parsing operation.
	 *
	 * This is invoked automatically by the Blueprint async action system after the node is created.
	 */
	virtual void Activate() override;
	
private:
	/**
	 * Parser instance used to evaluate the player's input.
	 *
	 * Stored as a UPROPERTY so the object remains referenced while the async action runs.
	 */
	UPROPERTY()
	USemanticParser* Parser;
	
	/**
	 * Cached player input string that will be passed to the semantic parser.
	 */
	FString InputText;

	/**
	 * Confidence threshold used to determine whether parsing succeeded.
	 */
	float Threshold;
};