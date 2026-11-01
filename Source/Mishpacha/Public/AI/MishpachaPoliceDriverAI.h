// MISHPACHA - drives a police car to the wanted player along the street grid.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MishpachaPoliceDriverAI.generated.h"

class AMishpachaPoliceCar;

/**
 * Asset-free driver: re-plans a route on the gray-box street grid (ACityGreyboxGenerator::FindRoadRoute,
 * straight line on other levels), steers at a look-ahead point, slows for turns, backs out when stuck,
 * and stops to deploy the officers once it's close (or immediately for a roadblock).
 */
UCLASS()
class MISHPACHA_API AMishpachaPoliceDriverAI : public AAIController
{
	GENERATED_BODY()

public:
	AMishpachaPoliceDriverAI();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	/** Stop and deploy this close to the suspect (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Driving")
	float DeployDistance = 2200.f;

	UPROPERTY(EditDefaultsOnly, Category = "Driving")
	float CruiseSpeed = 1700.f;

	UPROPERTY(EditDefaultsOnly, Category = "Driving")
	float TurnSpeed = 700.f;

	UPROPERTY(EditDefaultsOnly, Category = "Driving")
	float LookAhead = 900.f;

private:
	FTimerHandle DriveTimer;
	TArray<FVector> Route;
	double LastPlanTime = -100.0;
	float StuckTime = 0.f;
	float ReverseTimeLeft = 0.f;

	void Drive();
	APawn* FindSuspect(const AMishpachaPoliceCar* Car) const;
	FVector GetLookAheadPoint(const FVector& From) const;
	void Park(AMishpachaPoliceCar* Car);
};
