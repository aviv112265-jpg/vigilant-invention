// MISHPACHA - a civilian car driven by AMishpachaTrafficAI (spawned around players by the gray-box city).
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MishpachaVehicle.h"
#include "MishpachaTrafficCar.generated.h"

UCLASS()
class MISHPACHA_API AMishpachaTrafficCar : public AMishpachaVehicle
{
	GENERATED_BODY()

public:
	AMishpachaTrafficCar();
};
