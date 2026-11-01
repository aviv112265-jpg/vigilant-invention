// MISHPACHA - Shira's ability: "Connections" - one phone call and the patrols look elsewhere.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/SiblingAbilityComponent.h"
#include "ShiraConnectionsAbility.generated.h"

UCLASS(ClassGroup = (Mishpacha), meta = (BlueprintSpawnableComponent))
class MISHPACHA_API UShiraConnectionsAbility : public USiblingAbilityComponent
{
	GENERATED_BODY()

public:
	UShiraConnectionsAbility();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Ability|Connections", meta = (ClampMin = "1", ClampMax = "5"))
	int32 LevelsToReduce = 2;

	/** At this Heat level or above nobody picks up the phone. */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|Connections", meta = (ClampMin = "1", ClampMax = "5"))
	int32 UnusableAtHeatLevel = 5;

	virtual bool CanActivate(FText& OutReason) const override;
	virtual void ActivateAbility() override;
};
