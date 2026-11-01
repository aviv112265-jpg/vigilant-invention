// MISHPACHA - spawns and directs police based on players' Heat.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PoliceDispatchSubsystem.generated.h"

class APawn;
class UPoliceDispatchConfig;

/** Plain bookkeeping record (not reflected). */
struct FPoliceUnitRecord
{
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<APawn> Target;
};

/**
 * Server-only, refreshes once per second:
 *  - removes stale units, spawns reinforcements up to the per-level cap,
 *  - sends units toward the last known location,
 *  - tells the Heat component when an officer can see the player.
 * Real police behaviour (shooting, roadblocks, helicopters) belongs in the units' AI (StateTree/Behavior Tree).
 */
UCLASS()
class MISHPACHA_API UPoliceDispatchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Police")
	int32 GetActiveUnitCount() const { return Units.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Police")
	void DespawnAllUnits();

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	TArray<FPoliceUnitRecord> Units;
	FTimerHandle RefreshTimer;

	void Refresh();
	const UPoliceDispatchConfig* GetConfig() const;
	bool TrySpawnUnit(const UPoliceDispatchConfig& Config, TSubclassOf<APawn> UnitClass, APawn* Target);
	bool FindSpawnLocation(const UPoliceDispatchConfig& Config, const FVector& Origin, FVector& OutLocation) const;
	bool HasLineOfSight(const AActor* From, const AActor* To) const;
	static void DespawnUnit(const FPoliceUnitRecord& Unit);
};
