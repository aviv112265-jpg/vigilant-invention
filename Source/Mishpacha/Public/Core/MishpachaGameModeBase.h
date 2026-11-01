// MISHPACHA - rules shared by Story and Online modes.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MishpachaGameModeBase.generated.h"

class AMishpachaCharacter;
class UPoliceDispatchConfig;

/**
 * Shared rules: hospital respawn (with a bill), police configuration, default classes.
 * Hospitals are any actors tagged "Hospital" (falls back to PlayerStarts).
 */
UCLASS(Abstract)
class MISHPACHA_API AMishpachaGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMishpachaGameModeBase();

	UFUNCTION(BlueprintPure, Category = "Police")
	UPoliceDispatchConfig* GetPoliceConfig() const { return PoliceConfig; }

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;

	/**
	 * Zero-setup mode: if the level has no ACityGreyboxGenerator and no ASiblingSpawnPoint,
	 * spawn a procedural gray-box city (roads, buildings, beach, hospital, missions, pedestrians).
	 * Safe to call repeatedly; runs once.
	 */
	void EnsureAutoContent();

	/** Called by AMishpachaCharacter on the server when it dies. VictimController may be null. */
	virtual void OnCharacterDied(AMishpachaCharacter* DeadCharacter, AController* VictimController, AController* Killer);

protected:
	/** Leave empty to use built-in officers (AMishpachaPolicePawn) - see bAutoPolice. */
	UPROPERTY(EditDefaultsOnly, Category = "Police")
	TObjectPtr<UPoliceDispatchConfig> PoliceConfig;

	/** Create a default police response when PoliceConfig is empty. */
	UPROPERTY(EditDefaultsOnly, Category = "Police")
	bool bAutoPolice = true;

	/** Spawn the gray-box city when the level isn't set up (no generator and no sibling spawn points). */
	UPROPERTY(EditDefaultsOnly, Category = "Auto Setup")
	bool bAutoGreybox = true;

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn", meta = (ClampMin = "0.1"))
	float RespawnDelay = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn", meta = (ClampMin = "0"))
	int32 HospitalFee = 500;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn")
	FName HospitalTag = TEXT("Hospital");

	virtual void RespawnPlayer(AController* DeadController, const FVector& DeathLocation);

	FTransform FindHospitalTransform(const FVector& NearLocation, AController* ForController);

private:
	bool bAutoContentChecked = false;
};
