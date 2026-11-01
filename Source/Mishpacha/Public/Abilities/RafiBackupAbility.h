// MISHPACHA - Rafi's ability: "Call the boys" - family soldiers show up and follow him.
#pragma once

#include "CoreMinimal.h"
#include "Abilities/SiblingAbilityComponent.h"
#include "RafiBackupAbility.generated.h"

class APawn;

UCLASS(ClassGroup = (Mishpacha), meta = (BlueprintSpawnableComponent))
class MISHPACHA_API URafiBackupAbility : public USiblingAbilityComponent
{
	GENERATED_BODY()

public:
	URafiBackupAbility();

protected:
	/** NPC class for the family soldiers (give it an AI controller + weapon in its Blueprint). */
	UPROPERTY(EditDefaultsOnly, Category = "Ability|Backup")
	TSubclassOf<APawn> BackupPawnClass;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Backup", meta = (ClampMin = "1", ClampMax = "8"))
	int32 NumBackup = 2;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Backup")
	float SpawnRadius = 600.f;

	UPROPERTY(EditDefaultsOnly, Category = "Ability|Backup")
	float FollowDistance = 250.f;

	virtual bool CanActivate(FText& OutReason) const override;
	virtual void ActivateAbility() override;
	virtual void DeactivateAbility() override;

private:
	UPROPERTY()
	TArray<TObjectPtr<APawn>> SpawnedBackup;

	FTimerHandle FollowTimer;

	void UpdateFollow();
};
