// MISHPACHA - a story mission / job authored as a Data Asset.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MissionDefinition.generated.h"

class UMissionObjective;

UCLASS(BlueprintType)
class MISHPACHA_API UMissionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable id used by saves and prerequisites, e.g. "Rafi_01_Parking". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FName MissionId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (MultiLine = "true"))
	FText Description;

	/** "Rafi", "Shira", "Omer" - or None for any character (online players too). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	FName RequiredSibling;

	/** Allow switching siblings mid-mission (multi-sibling jobs). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bAllowSwitching = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	bool bRepeatable = false;

	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Mission")
	TArray<TObjectPtr<UMissionObjective>> Objectives;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission", meta = (ClampMin = "0"))
	int32 RewardMoney = 1000;

	/** Mission ids that must be completed first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission")
	TArray<FName> Prerequisites;
};
