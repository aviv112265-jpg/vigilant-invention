// MISHPACHA - asset-free police officer brain.
#pragma once

#include "CoreMinimal.h"
#include "AI/MishpachaNPCAIController.h"
#include "MishpachaPoliceAIController.generated.h"

class UHeatComponent;

/**
 * Timer-driven police logic (no Behavior Tree assets needed):
 *  - target = the wanted player assigned by UPoliceDispatchSubsystem, or the nearest wanted player,
 *  - sees the target -> reports it to the target's Heat, chases it, and shoots from Heat level 3,
 *  - lost sight -> searches the last known location,
 *  - Heat back to 0 -> stands down.
 */
UCLASS()
class MISHPACHA_API AMishpachaPoliceAIController : public AMishpachaNPCAIController
{
	GENERATED_BODY()

public:
	AMishpachaPoliceAIController();

	void SetTarget(APawn* NewTarget);
	APawn* GetTarget() const { return Target.Get(); }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UPROPERTY(EditDefaultsOnly, Category = "Police")
	float SightDistance = 5000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Police")
	float ShootDistance = 2500.f;

	/** Heat level from which officers open fire. */
	UPROPERTY(EditDefaultsOnly, Category = "Police")
	int32 ShootFromHeatLevel = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Police")
	float ThinkInterval = 0.4f;

private:
	TWeakObjectPtr<APawn> Target;
	FTimerHandle ThinkTimer;
	bool bFiring = false;

	void Think();
	APawn* FindNearestWantedPlayer() const;
	bool CanSee(const AActor* Other) const;
	void SetFiring(bool bNewFiring);
};
