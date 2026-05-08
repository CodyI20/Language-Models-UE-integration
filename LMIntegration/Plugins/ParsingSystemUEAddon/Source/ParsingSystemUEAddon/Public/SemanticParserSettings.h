#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/DataTable.h"
#include "NNEModelData.h"
#include "SemanticParserSettings.generated.h"

UCLASS(Config=Game, defaultconfig, meta=(DisplayName="Semantic Parser Settings"))
class PARSINGSYSTEMUEADDON_API USemanticParserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USemanticParserSettings()
	{
		CategoryName = TEXT("Plugins");
		SectionName = TEXT("Semantic Parser");
	}

	UPROPERTY(Config, EditAnywhere, Category = "Model")
	TSoftObjectPtr<UNNEModelData> DefaultModel;

	UPROPERTY(Config, EditAnywhere, Category = "Testing")
	TSoftObjectPtr<UDataTable> TestCommandAliasesTable;

	UPROPERTY(Config, EditAnywhere, Category = "Testing")
	TSoftObjectPtr<UDataTable> TestEvaluationCasesTable;
};