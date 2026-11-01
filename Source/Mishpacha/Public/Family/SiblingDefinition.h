// MISHPACHA - data describing one playable Sela sibling.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "SiblingDefinition.generated.h"

class AMishpachaCharacter;
class USiblingAbilityComponent;
class UTexture2D;

/**
 * One Data Asset per sibling: DA_Rafi, DA_Shira, DA_Omer.
 * Assign them (in switch order) to BP_StoryGameMode > Family Definitions.
 */
UCLASS(BlueprintType)
class MISHPACHA_API USiblingDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable id used by saves, spawn points and missions: "Rafi", "Shira", "Omer". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling")
	FName SiblingId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling")
	FText DisplayName;

	/** One-liner for the switch menu, e.g. "The big brother. Runs the business." */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling")
	FText Tagline;

	/** Blueprint child of AMishpachaCharacter with this sibling's mesh/animations (BP_Rafi ...). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling")
	TSubclassOf<AMishpachaCharacter> PawnClass;

	/** Added at spawn if the pawn Blueprint doesn't already have an ability component. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling")
	TSubclassOf<USiblingAbilityComponent> AbilityClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling", meta = (ClampMin = "0"))
	int32 StartingMoney = 5000;

	/** UI accent color for this sibling (switch menu, HUD). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling|UI")
	FLinearColor AccentColor = FLinearColor::White;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sibling|UI")
	TSoftObjectPtr<UTexture2D> Portrait;
};
