// MISHPACHA - shared mission data types.
#pragma once

#include "CoreMinimal.h"
#include "MissionTypes.generated.h"

/** Mission progress mirrored to every client through AMishpachaGameState (for the HUD). */
USTRUCT(BlueprintType)
struct FReplicatedMissionState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FName MissionId;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FText Title;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FText ObjectiveText;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	int32 ObjectiveIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	int32 ObjectiveCount = 0;

	/** Sibling this job belongs to (None = anyone) - the HUD tints the job card with their color. */
	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FName RequiredSibling;

	/** Where the current objective wants the player to go (GPS route, map blip). */
	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	bool bHasGuide = false;

	UPROPERTY(BlueprintReadOnly, Category = "Mission")
	FVector GuideLocation = FVector::ZeroVector;
};
