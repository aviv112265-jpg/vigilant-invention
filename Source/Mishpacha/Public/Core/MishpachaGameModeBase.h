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

	/** Called by AMishpachaCharacter on the server when it dies. VictimController may be null. */
	virtual void OnCharacterDied(AMishpachaCharacter* DeadCharacter, AController* VictimController, AController* Killer);

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Police")
	TObjectPtr<UPoliceDispatchConfig> PoliceConfig;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn", meta = (ClampMin = "0.1"))
	float RespawnDelay = 5.f;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn", meta = (ClampMin = "0"))
	int32 HospitalFee = 500;

	UPROPERTY(EditDefaultsOnly, Category = "Respawn")
	FName HospitalTag = TEXT("Hospital");

	virtual void RespawnPlayer(AController* DeadController, const FVector& DeathLocation);

	FTransform FindHospitalTransform(const FVector& NearLocation, AController* ForController);
};
