// MISHPACHA - map data the HUD draws (minimap / full map), filled by the city generator.
#pragma once

#include "CoreMinimal.h"

/** Paint order on the map texture follows the order the areas were added. */
enum class EMishpachaMapAreaType : uint8
{
	Sea,
	Sand,
	Road,
	Block,
	Plaza,
	Building,
	Tower
};

/** Axis-aligned world-space rectangle (X/Y) of one kind of ground. */
struct FMishpachaMapArea
{
	FBox2D Box = FBox2D(ForceInit);
	EMishpachaMapAreaType Type = EMishpachaMapAreaType::Block;
};

/** Named place shown with a label on the full map. */
struct FMishpachaMapPlace
{
	FVector Location = FVector::ZeroVector;
	FText Label;
};
