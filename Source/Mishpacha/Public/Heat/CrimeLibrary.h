// MISHPACHA - helpers for reporting crimes and witness checks.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Heat/CrimeTypes.h"
#include "CrimeLibrary.generated.h"

class UHeatComponent;

/**
 * Tag conventions used across the project:
 *  - "Civilian" : NPCs that can witness crimes
 *  - "Police"   : police units (witnesses too; killing one is CopKilled)
 *  - "Enemy"    : hostile NPCs (Omer's drone can mark them)
 */
UCLASS()
class MISHPACHA_API UCrimeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True if a pawn tagged "Civilian" or "Police" within Radius has line of sight to Location. */
	UFUNCTION(BlueprintCallable, Category = "Heat", meta = (WorldContext = "WorldContextObject"))
	static bool IsWitnessed(const UObject* WorldContextObject, FVector Location, AActor* Offender, float Radius = 3000.f);

	/** Finds the offender's Heat component and reports the crime (server only). */
	UFUNCTION(BlueprintCallable, Category = "Heat")
	static void ReportCrimeByActor(AActor* Offender, ECrimeType Crime, FVector Location, bool bAlwaysWitnessed = false);

	/** Resolves pawn / controller / player state / owned actor (e.g. weapon) to its Heat component. */
	UFUNCTION(BlueprintPure, Category = "Heat")
	static UHeatComponent* FindHeatComponent(const AActor* Actor);
};
