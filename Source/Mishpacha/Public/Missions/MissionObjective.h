// MISHPACHA - base mission objective (instanced inside a UMissionDefinition).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MissionObjective.generated.h"

class APawn;
class APlayerController;
class UMissionSubsystem;

/**
 * Objectives are authored inline in a Mission Definition asset and duplicated per run,
 * so they can safely hold runtime state. Subclass in C++ or Blueprint for new objective types.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType, Blueprintable)
class MISHPACHA_API UMissionObjective : public UObject
{
	GENERATED_BODY()

public:
	/** Shown on the HUD, e.g. "Pick up Shira at the courthouse". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FText Description;

	virtual void Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer);
	virtual void TickObjective(float DeltaTime) {}
	virtual void Deactivate() {}
	virtual bool IsComplete() const { return bComplete; }
	virtual bool HasFailed() const { return false; }

	/** Where the player should head for this objective (GPS route / map). False = no single place. */
	virtual bool GetGuideLocation(FVector& OutLocation) const { return false; }

	virtual UWorld* GetWorld() const override;

protected:
	TWeakObjectPtr<UMissionSubsystem> Missions;
	TWeakObjectPtr<APlayerController> Player;
	bool bComplete = false;

	APawn* GetPlayerPawn() const;
	void MarkComplete() { bComplete = true; }
};
