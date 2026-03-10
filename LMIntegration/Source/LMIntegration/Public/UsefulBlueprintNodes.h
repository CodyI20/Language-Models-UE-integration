// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UsefulBlueprintNodes.generated.h"

/**
 * 
 */
UCLASS()
class LMINTEGRATION_API UUsefulBlueprintNodes : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "Utilities|PrintString")
	static void ClearOnScreenDebugMessages();
};
