// MISHPACHA - built-in objective types.
#pragma once

#include "CoreMinimal.h"
#include "Missions/MissionObjective.h"
#include "MissionObjectives.generated.h"

/** Get the player (on foot or in a vehicle) within Radius of a tagged actor or a fixed location. */
UCLASS(meta = (DisplayName = "Reach Location"))
class MISHPACHA_API UObjective_ReachLocation : public UMissionObjective
{
	GENERATED_BODY()

public:
	/** If set, the first actor with this tag is the destination; otherwise TargetLocation is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName TargetActorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "50"))
	float Radius = 500.f;

	virtual void Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer) override;
	virtual void TickObjective(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "Objective")
	FVector GetDestination() const;

private:
	TWeakObjectPtr<AActor> TargetActor;
};

/** Get into a vehicle (optionally one with a specific tag). */
UCLASS(meta = (DisplayName = "Enter Vehicle"))
class MISHPACHA_API UObjective_EnterVehicle : public UMissionObjective
{
	GENERATED_BODY()

public:
	/** None = any vehicle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName VehicleTag;

	virtual void TickObjective(float DeltaTime) override;
};

/** Kill / destroy actors with a tag. */
UCLASS(meta = (DisplayName = "Eliminate Targets"))
class MISHPACHA_API UObjective_EliminateTargets : public UMissionObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName TargetTag;

	/** 0 = all tagged actors present when the objective starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0"))
	int32 RequiredCount = 0;

	virtual void Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer) override;
	virtual void TickObjective(float DeltaTime) override;

private:
	TArray<TWeakObjectPtr<AActor>> Targets;
	int32 Required = 0;
};

/** Lose the police (Heat back to 0). */
UCLASS(meta = (DisplayName = "Lose The Heat"))
class MISHPACHA_API UObjective_LoseHeat : public UMissionObjective
{
	GENERATED_BODY()

public:
	virtual void TickObjective(float DeltaTime) override;
};
