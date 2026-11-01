// MISHPACHA - Story mode: the three Sela siblings, switchable.
#pragma once

#include "CoreMinimal.h"
#include "Core/MishpachaGameModeBase.h"
#include "StoryGameMode.generated.h"

class USiblingDefinition;

/**
 * Story mode. The first player to join controls the Sela family (Rafi, Shira, Omer) and can switch between them.
 * Create a Blueprint child (BP_StoryGameMode) and fill FamilyDefinitions with the three sibling Data Assets.
 */
UCLASS()
class MISHPACHA_API AStoryGameMode : public AMishpachaGameModeBase
{
	GENERATED_BODY()

public:
	AStoryGameMode();

	/** Writes the family state and mission progress to disk. */
	UFUNCTION(BlueprintCallable, Category = "Story")
	void SaveProgress();

	virtual void OnCharacterDied(AMishpachaCharacter* DeadCharacter, AController* VictimController, AController* Killer) override;

protected:
	/** Order = switch keys 1/2/3. Recommended: Rafi, Shira, Omer. */
	UPROPERTY(EditDefaultsOnly, Category = "Story")
	TArray<TObjectPtr<USiblingDefinition>> FamilyDefinitions;

	/** Load the last save when the level starts (turn off to always start fresh). */
	UPROPERTY(EditDefaultsOnly, Category = "Story")
	bool bLoadSaveOnStart = true;

	UPROPERTY(EditDefaultsOnly, Category = "Story")
	bool bAutosaveOnSwitch = true;

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RespawnPlayer(AController* DeadController, const FVector& DeathLocation) override;

	UFUNCTION()
	void HandleSiblingSwitched(int32 OldIndex, int32 NewIndex);
};
