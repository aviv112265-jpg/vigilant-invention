// MISHPACHA - asset-free rival crew brain.
#pragma once

#include "CoreMinimal.h"
#include "AI/MishpachaNPCAIController.h"
#include "MishpachaRivalAIController.generated.h"

/**
 * Timer-driven gang member (no Behavior Tree assets needed):
 *  - guards the spot it was spawned at,
 *  - a player inside AggroRadius that it can see becomes the target: it closes in and shoots,
 *  - loses the target -> walks back to its spot.
 */
UCLASS()
class MISHPACHA_API AMishpachaRivalAIController : public AMishpachaNPCAIController
{
	GENERATED_BODY()

public:
	AMishpachaRivalAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UPROPERTY(EditDefaultsOnly, Category = "Rival")
	float AggroRadius = 3500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rival")
	float ShootDistance = 2600.f;

	UPROPERTY(EditDefaultsOnly, Category = "Rival")
	float ThinkInterval = 0.35f;

private:
	FVector GuardSpot = FVector::ZeroVector;
	TWeakObjectPtr<APawn> Target;
	FTimerHandle ThinkTimer;
	bool bFiring = false;

	void Think();
	APawn* FindTarget() const;
	bool CanSee(const AActor* Other) const;
	void SetFiring(bool bNewFiring);
};
