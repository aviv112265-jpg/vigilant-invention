// MISHPACHA - Omer's ability: deploy his drone, fly it, and mark enemies.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/SiblingAbilityComponent.h"
#include "OmerDroneAbility.generated.h"

class ADronePawn;

UCLASS(ClassGroup = (Mishpacha), meta = (BlueprintSpawnableComponent))
class MISHPACHA_API UOmerDroneAbility : public USiblingAbilityComponent
{
	GENERATED_BODY()

public:
	UOmerDroneAbility();

	UFUNCTION(BlueprintPure, Category = "Ability|Drone")
	ADronePawn* GetActiveDrone() const { return ActiveDrone.Get(); }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Ability|Drone")
	TSubclassOf<ADronePawn> DroneClass;

	/** Launch offset relative to Omer (local space). */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|Drone")
	FVector SpawnOffset = FVector(50.f, 0.f, 200.f);

	virtual bool CanActivate(FText& OutReason) const override;
	virtual void ActivateAbility() override;
	virtual void DeactivateAbility() override;

private:
	TWeakObjectPtr<ADronePawn> ActiveDrone;
};
