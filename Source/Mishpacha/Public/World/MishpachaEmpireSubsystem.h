// MISHPACHA - "the business": Rafi's holdings, their income, and the rival crews that come for them.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MishpachaEmpireSubsystem.generated.h"

class AMishpachaBusiness;
class APlayerController;

/**
 * Server only, story mode only. Owned businesses pay Rafi on a timer. Every few minutes a rival crew
 * attacks one: kill them before the clock runs out, or the business is lost (and can be bought back
 * for half the price). Ownership lives in the save game.
 */
UCLASS()
class MISHPACHA_API UMishpachaEmpireSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Called by the city for each business it spawns: restores it from the save and starts the clocks. */
	void RegisterBusiness(AMishpachaBusiness* Business);

	/** Rafi buys it, if he's the one standing there and can pay. */
	bool TryBuy(AMishpachaBusiness* Business, APlayerController* Buyer);

	void PayIncome();

	/** Attack this business, or a random owned one. False if there's nothing to attack. */
	bool StartAttack(AMishpachaBusiness* Target = nullptr);

	/** Close the running attack: defended, or the business is lost. */
	void EndAttack(bool bDefended);

	int32 GetOwnedCount() const;
	const TArray<TWeakObjectPtr<AMishpachaBusiness>>& GetBusinesses() const { return Businesses; }

	UPROPERTY(EditDefaultsOnly, Category = "Empire")
	float IncomeInterval = 120.f;

	UPROPERTY(EditDefaultsOnly, Category = "Empire")
	float AttackDuration = 150.f;

	UPROPERTY(EditDefaultsOnly, Category = "Empire")
	FVector2D AttackDelay = FVector2D(240.f, 420.f);

	/** Paid to Rafi for fighting off a crew. */
	UPROPERTY(EditDefaultsOnly, Category = "Empire")
	int32 DefenseReward = 1000;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	TArray<TWeakObjectPtr<AMishpachaBusiness>> Businesses;
	TWeakObjectPtr<AMishpachaBusiness> Attacked;
	TArray<TWeakObjectPtr<APawn>> Crew;
	FTimerHandle IncomeTimer;
	FTimerHandle AttackTimer;
	FTimerHandle AttackCheckTimer;

	void StartTimers();
	void ScheduleAttack(float Delay);
	void RandomAttack();
	void CheckAttack();
	void UpdateSave(const AMishpachaBusiness* Business);
	void Notify(const FText& Message) const;
	class UMishpachaSaveGame* GetSave() const;
};
