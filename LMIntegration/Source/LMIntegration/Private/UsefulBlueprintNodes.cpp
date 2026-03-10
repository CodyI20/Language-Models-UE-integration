// Fill out your copyright notice in the Description page of Project Settings.

#include "LMIntegration/Public/UsefulBlueprintNodes.h"


void UUsefulBlueprintNodes::ClearOnScreenDebugMessages()
{
	if (GEngine == nullptr)
		return;
	GEngine->ClearOnScreenDebugMessages();
}
