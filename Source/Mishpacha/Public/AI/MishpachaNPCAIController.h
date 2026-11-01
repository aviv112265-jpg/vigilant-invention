// MISHPACHA - base AI controller with a "no navmesh" fallback.
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MishpachaNPCAIController.generated.h"

/**
 * Behaves like a normal AAIController, but when a MoveTo request fails (typically because the
 * level has no NavMeshBoundsVolume yet) it steers the pawn straight at the goal with simple
 * obstacle side-stepping. Gray-box levels work without any navigation setup; add a
 * NavMeshBoundsVolume later and real path finding takes over automatically.
 */
UCLASS()
class MISHPACHA_API AMishpachaNPCAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMishpachaNPCAIController();

	virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath = nullptr) override;
	virtual void StopMovement() override;
	virtual void Tick(float DeltaSeconds) override;

	/** True while the fallback steering is driving the pawn. */
	bool IsDirectSteering() const { return bDirectSteering; }

protected:
	virtual void OnUnPossess() override;

	/** Gives up direct steering after this long without reaching the goal (s). */
	UPROPERTY(EditDefaultsOnly, Category = "AI")
	float DirectSteerTimeout = 20.f;

private:
	bool bDirectSteering = false;
	TWeakObjectPtr<AActor> SteerGoalActor;
	FVector SteerGoalLocation = FVector::ZeroVector;
	float SteerAcceptance = 100.f;
	float SteerElapsed = 0.f;
	float AvoidSign = 1.f;
	float AvoidTimeLeft = 0.f;

	void ClearDirectSteering();
};
