// MISHPACHA - asset-free pedestrian brain.
#pragma once

#include "CoreMinimal.h"
#include "AI/MishpachaNPCAIController.h"
#include "MishpachaCivilianAIController.generated.h"

/** Wanders around its home point; runs away from gunshots heard within FleeHearingRadius. */
UCLASS()
class MISHPACHA_API AMishpachaCivilianAIController : public AMishpachaNPCAIController
{
	GENERATED_BODY()

public:
	AMishpachaCivilianAIController();

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "Civilian")
	float WanderRadius = 1500.f;

	UPROPERTY(EditDefaultsOnly, Category = "Civilian")
	float FleeHearingRadius = 4000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Civilian")
	float FleeDuration = 8.f;

private:
	FVector HomeLocation = FVector::ZeroVector;
	FTimerHandle WanderTimer;
	FDelegateHandle GunshotHandle;
	double FleeUntil = 0.0;

	void Wander();
	void HandleGunshot(UWorld* World, const FVector& Location);
	void Unsubscribe();
};
