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
	virtual bool GetGuideLocation(FVector& OutLocation) const override;

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

	/** The nearest matching vehicle. */
	virtual bool GetGuideLocation(FVector& OutLocation) const override;
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

	/** The nearest target still standing. */
	virtual bool GetGuideLocation(FVector& OutLocation) const override;

private:
	TArray<TWeakObjectPtr<AActor>> Targets;
	int32 Required = 0;
};

/** Spawn a rival crew around a tagged place and take them all out. */
UCLASS(meta = (DisplayName = "Rival Crew"))
class MISHPACHA_API UObjective_RivalCrew : public UMissionObjective
{
	GENERATED_BODY()

public:
	UObjective_RivalCrew();

	/** First actor with this tag is where the crew hangs out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName LocationTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "1", ClampMax = "12"))
	int32 Count = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	float SpreadRadius = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	TSubclassOf<APawn> RivalClass;

	virtual void Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer) override;
	virtual void TickObjective(float DeltaTime) override;
	virtual bool GetGuideLocation(FVector& OutLocation) const override;

private:
	TArray<TWeakObjectPtr<APawn>> Crew;
	FVector CrewSpot = FVector::ZeroVector;
};

/** Hand over to another sibling (the mission must have bAllowSwitching). */
UCLASS(meta = (DisplayName = "Switch To Sibling"))
class MISHPACHA_API UObjective_SwitchToSibling : public UMissionObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName SiblingId;

	virtual void TickObjective(float DeltaTime) override;
};

/** Stay near a tagged place for a while (a meeting, a pickup). */
UCLASS(meta = (DisplayName = "Wait At"))
class MISHPACHA_API UObjective_WaitAt : public UMissionObjective
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective")
	FName LocationTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "0"))
	float Seconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Objective", meta = (ClampMin = "50"))
	float Radius = 800.f;

	virtual void Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer) override;
	virtual void TickObjective(float DeltaTime) override;
	virtual bool GetGuideLocation(FVector& OutLocation) const override;

private:
	FVector Spot = FVector::ZeroVector;
	float Waited = 0.f;
};

/** Lose the police (Heat back to 0). */
UCLASS(meta = (DisplayName = "Lose The Heat"))
class MISHPACHA_API UObjective_LoseHeat : public UMissionObjective
{
	GENERATED_BODY()

public:
	virtual void TickObjective(float DeltaTime) override;
};
