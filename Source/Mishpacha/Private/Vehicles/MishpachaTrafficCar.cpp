// MISHPACHA - civilian traffic car.
#include "Vehicles/MishpachaTrafficCar.h"
#include "AI/MishpachaTrafficAI.h"

AMishpachaTrafficCar::AMishpachaTrafficCar()
{
	Tags.AddUnique(FName(TEXT("Traffic")));
	AIControllerClass = AMishpachaTrafficAI::StaticClass();
	AutoPossessAI = EAutoPossessAI::Spawned;
}
