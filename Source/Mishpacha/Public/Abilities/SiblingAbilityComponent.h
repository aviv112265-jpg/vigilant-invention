// MISHPACHA - base class for each sibling's special ability.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SiblingAbilityComponent.generated.h"

class APawn;
class AController;
class UHeatComponent;

/**
 * Replicated, server-authoritative ability with cooldown and optional duration.
 * Subclasses override CanActivate / ActivateAbility / DeactivateAbility.
 * Add one to a character Blueprint, or let USiblingDefinition::AbilityClass add it at spawn.
 */
UCLASS(Abstract, Blueprintable, ClassGroup = (Mishpacha), meta = (BlueprintSpawnableComponent))
class MISHPACHA_API USiblingAbilityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USiblingAbilityComponent();

	/** Call from the owning client or the server. */
	UFUNCTION(BlueprintCallable, Category = "Ability")
	void TryActivate();

	/** Server: ends the ability now (e.g. owner died, drone recalled). */
	void ForceEnd();

	UFUNCTION(BlueprintPure, Category = "Ability")
	bool IsAbilityActive() const { return bAbilityActive; }

	UFUNCTION(BlueprintPure, Category = "Ability")
	float GetCooldownRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Ability")
	FText GetAbilityName() const { return AbilityName; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server. Return false and fill OutReason to refuse activation. */
	virtual bool CanActivate(FText& OutReason) const;
	/** Server. */
	virtual void ActivateAbility() {}
	/** Server. */
	virtual void DeactivateAbility() {}

	/** Server: finishes the ability (called automatically after Duration). */
	void EndAbility();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability")
	FText AbilityName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0"))
	float Cooldown = 30.f;

	/** 0 = instant ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ability", meta = (ClampMin = "0"))
	float Duration = 15.f;

	UPROPERTY(ReplicatedUsing = OnRep_AbilityActive)
	bool bAbilityActive = false;

	/** Server world time of the last activation. */
	UPROPERTY(Replicated)
	double LastActivationTime = -1000000.0;

	UFUNCTION(Server, Reliable)
	void ServerTryActivate();

	UFUNCTION()
	void OnRep_AbilityActive();

	UFUNCTION(BlueprintImplementableEvent, Category = "Ability")
	void BP_OnActivated();

	UFUNCTION(BlueprintImplementableEvent, Category = "Ability")
	void BP_OnDeactivated();

	APawn* GetOwnerPawn() const;
	AController* GetOwnerController() const;
	UHeatComponent* GetOwnerHeat() const;
	double GetServerTime() const;
	void NotifyOwner(const FText& Message) const;

private:
	FTimerHandle DurationTimer;

	void HandleActivateRequest();
};
