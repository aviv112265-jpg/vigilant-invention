// MISHPACHA - civilian driver: keeps right, wanders the street grid, stops at red lights, brakes for whatever is ahead.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MishpachaTrafficAI.generated.h"

class ACityGreyboxGenerator;
class AMishpachaVehicle;

/**
 * Drives from intersection to intersection of the gray-box grid in the right-hand lane, picking a
 * random way on at each junction (no U-turns unless it's a dead end). A sweep ahead of the bumper
 * brakes for cars, people and players; stuck for a while -> backs up. Carjacking throws it out
 * (AMishpachaVehicle::EnterVehicle), leaving the car parked.
 */
UCLASS()
class MISHPACHA_API AMishpachaTrafficAI : public AAIController
{
	GENERATED_BODY()

public:
	AMishpachaTrafficAI();

	/** Where the car is coming from / going to (grid indices). Set right after spawning. */
	void SetLeg(const FIntPoint& From, const FIntPoint& To);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UPROPERTY(EditDefaultsOnly, Category = "Traffic")
	float CruiseSpeed = 1100.f; // ~40 km/h

	UPROPERTY(EditDefaultsOnly, Category = "Traffic")
	float JunctionSpeed = 600.f;

	/** Right-hand lane: center offset from the road's center line (clear of cars parked at the curb). */
	UPROPERTY(EditDefaultsOnly, Category = "Traffic")
	float LaneOffset = 280.f;

private:
	TWeakObjectPtr<ACityGreyboxGenerator> Grid;
	FIntPoint FromNode = FIntPoint::ZeroValue;
	FIntPoint ToNode = FIntPoint::ZeroValue;
	FTimerHandle DriveTimer;
	float StuckTime = 0.f;
	float BlockedTime = 0.f;
	float ReverseTimeLeft = 0.f;
	bool bWaitingAtLight = false;

	void Drive();
	FVector LanePoint(const FIntPoint& Node, const FVector& Direction) const;
	void PickNextNode();
	bool IsBlockedAhead(const AMishpachaVehicle* Car, float Distance) const;
};
