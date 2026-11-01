// MISHPACHA - police response tuning per Heat level.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PoliceDispatchConfig.generated.h"

class APawn;

USTRUCT(BlueprintType)
struct FPoliceHeatResponse
{
	GENERATED_BODY()

	/** Pawn classes to pick from (patrol car, foot officers, special unit, helicopter...). Give them an AI controller in their Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police")
	TArray<TSubclassOf<APawn>> UnitClasses;

	/** Max simultaneous units hunting ONE wanted player at this level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police", meta = (ClampMin = "0"))
	int32 MaxUnits = 2;
};

/** Create one of these as a Data Asset and assign it on your GameMode Blueprint (Police Config). */
UCLASS(BlueprintType)
class MISHPACHA_API UPoliceDispatchConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Index 0 = Heat level 1 ... index 4 = Heat level 5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police")
	TArray<FPoliceHeatResponse> ResponseByLevel;

	/** Units spawn on a ring between these distances (cm) from the wanted player, out of sight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police")
	float MinSpawnDistance = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police")
	float MaxSpawnDistance = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police")
	float DespawnDistance = 40000.f;

	/** Officers "see" the player within this range when they have line of sight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police")
	float SightDistance = 6000.f;

	/** Hard cap for the whole world (performance). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Police", meta = (ClampMin = "0"))
	int32 MaxTotalUnits = 24;

	const FPoliceHeatResponse* GetResponse(int32 HeatLevel) const
	{
		return ResponseByLevel.IsValidIndex(HeatLevel - 1) ? &ResponseByLevel[HeatLevel - 1] : nullptr;
	}
};
