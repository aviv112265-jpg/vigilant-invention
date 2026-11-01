// MISHPACHA - patrol car: white, light bar, AI driver, drops officers off near the suspect.
#pragma once

#include "CoreMinimal.h"
#include "Vehicles/MishpachaVehicle.h"
#include "MishpachaPoliceCar.generated.h"

class UStaticMeshComponent;

/**
 * Spawned by UPoliceDispatchSubsystem from Heat 2. AMishpachaPoliceDriverAI drives it along the
 * street grid to the wanted player, stops nearby and DeployOfficers() puts officers on foot.
 * With bRoadblock it stays parked across an intersection (Heat 4+) and deploys right away.
 * Players can still carjack it (the AI driver gets thrown out like any other).
 */
UCLASS()
class MISHPACHA_API AMishpachaPoliceCar : public AMishpachaVehicle
{
	GENERATED_BODY()

public:
	AMishpachaPoliceCar();

	/** Parked across the road instead of chasing. Set before FinishSpawning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police")
	bool bRoadblock = false;

	UPROPERTY(EditDefaultsOnly, Category = "Police", meta = (ClampMin = "0", ClampMax = "4"))
	int32 OfficerCount = 2;

	UPROPERTY(EditDefaultsOnly, Category = "Police")
	TSubclassOf<APawn> OfficerClass;

	/** Server, once: officers get out next to the car and join the hunt. */
	void DeployOfficers(APawn* Target);

	bool HasDeployed() const { return bDeployed; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Police")
	TObjectPtr<UStaticMeshComponent> LightBarRed;

	UPROPERTY(VisibleAnywhere, Category = "Police")
	TObjectPtr<UStaticMeshComponent> LightBarBlue;

private:
	bool bDeployed = false;
};
