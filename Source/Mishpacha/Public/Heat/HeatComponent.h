// MISHPACHA - "Heat" (Hom), the wanted system. Levels 0-5.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Heat/CrimeTypes.h"
#include "HeatComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMishpachaHeatChanged, int32, NewLevel, int32, OldLevel);

/**
 * Lives on the PlayerState so it works for both Story and Online modes.
 * All mutation is server-only; HeatLevel replicates to clients for the HUD.
 * Heat drops to 0 once the player stays out of police sight for GetSearchDuration() seconds.
 */
UCLASS(ClassGroup = (Mishpacha), meta = (BlueprintSpawnableComponent))
class MISHPACHA_API UHeatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeatComponent();

	static constexpr int32 MaxHeatLevel = 5;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Heat")
	void ReportCrime(ECrimeType Crime, FVector Location, bool bWitnessed);

	/** Police can see the player right now: resets the escape timer. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Heat")
	void NotifySpottedByPolice(FVector SpottedLocation);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Heat")
	void ReduceHeatLevels(int32 Levels);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Heat")
	void ClearHeat();

	UFUNCTION(BlueprintPure, Category = "Heat")
	int32 GetHeatLevel() const { return HeatLevel; }

	UFUNCTION(BlueprintPure, Category = "Heat")
	FVector GetLastKnownLocation() const { return LastKnownLocation; }

	/** Seconds the player must stay unseen to lose the current heat. */
	UFUNCTION(BlueprintPure, Category = "Heat")
	float GetSearchDuration() const;

	/** 0..1 progress of the escape timer (for the HUD). */
	UFUNCTION(BlueprintPure, Category = "Heat")
	float GetEscapeProgress() const;

	UPROPERTY(BlueprintAssignable, Category = "Heat")
	FOnMishpachaHeatChanged OnHeatChanged;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Heat points needed for levels 1..5 (index 0 = level 1). */
	UPROPERTY(EditDefaultsOnly, Category = "Heat")
	TArray<float> LevelThresholds;

	/** Heat points added per crime when it is witnessed. */
	UPROPERTY(EditDefaultsOnly, Category = "Heat")
	TMap<ECrimeType, float> CrimePoints;

	/** Fraction of points added for UNwitnessed gunshots (people still hear them). */
	UPROPERTY(EditDefaultsOnly, Category = "Heat", meta = (ClampMin = "0", ClampMax = "1"))
	float AudibleCrimeMultiplier = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Heat", meta = (ClampMin = "1"))
	float BaseSearchDuration = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "Heat", meta = (ClampMin = "0"))
	float SearchDurationPerLevel = 6.f;

private:
	UPROPERTY(ReplicatedUsing = OnRep_HeatLevel)
	int32 HeatLevel = 0;

	UPROPERTY(Replicated)
	FVector LastKnownLocation = FVector::ZeroVector;

	UPROPERTY(Replicated)
	float TimeSinceSpotted = 0.f;

	/** Server-only accumulator. */
	float HeatPoints = 0.f;

	UFUNCTION()
	void OnRep_HeatLevel(int32 OldLevel);

	void SetHeatLevel(int32 NewLevel);
	int32 ComputeLevelFromPoints() const;
	bool IsServer() const;
};
