// MISHPACHA - crime types reported to the Heat (wanted) system.
#pragma once

#include "CoreMinimal.h"
#include "CrimeTypes.generated.h"

UENUM(BlueprintType)
enum class ECrimeType : uint8
{
	PropertyDamage	UMETA(DisplayName = "Property Damage"),
	Assault			UMETA(DisplayName = "Assault"),
	Gunshot			UMETA(DisplayName = "Gunshot"),
	VehicleTheft	UMETA(DisplayName = "Vehicle Theft"),
	Carjacking		UMETA(DisplayName = "Carjacking"),
	Murder			UMETA(DisplayName = "Murder"),
	CopKilled		UMETA(DisplayName = "Cop Killed")
};
