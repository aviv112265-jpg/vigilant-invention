// MISHPACHA - simple server-authoritative hitscan weapon.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MishpachaWeapon.generated.h"

class USkeletalMeshComponent;
class UDamageType;

/**
 * Client sends its camera ray to the server, the server validates and traces.
 * Create BP children (BP_Pistol, BP_Rifle) to assign meshes, sounds, muzzle flash (BP_OnFired).
 */
UCLASS()
class MISHPACHA_API AMishpachaWeapon : public AActor
{
	GENERATED_BODY()

public:
	AMishpachaWeapon();

	/** Owning client only. */
	void StartFire();
	void StopFire();

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetAmmo() const { return Ammo; }

	UFUNCTION(BlueprintPure, Category = "Weapon")
	int32 GetMagazineSize() const { return MagazineSize; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Damage = 25.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Range = 10000.f;

	/** Shots per second. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon", meta = (ClampMin = "0.1"))
	float FireRate = 6.f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	bool bAutomatic = false;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon", meta = (ClampMin = "1"))
	int32 MagazineSize = 15;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float ReloadTime = 1.6f;

	/** Full cone angle in degrees. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float SpreadDegrees = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TSubclassOf<UDamageType> DamageTypeClass;

	UPROPERTY(ReplicatedUsing = OnRep_Ammo)
	int32 Ammo = 0;

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize TraceStart, FVector_NetQuantizeNormal TraceDirection);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireEffects(FVector_NetQuantize ImpactPoint);

	UFUNCTION()
	void OnRep_Ammo();

	/** Cosmetic hook: muzzle flash, tracer, sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
	void BP_OnFired(FVector ImpactPoint);

	UFUNCTION(BlueprintImplementableEvent, Category = "Weapon")
	void BP_OnAmmoChanged(int32 NewAmmo);

private:
	FTimerHandle AutoFireTimer;
	FTimerHandle ReloadTimer;
	double LastFireTime = -1000.0;
	double LastCrimeReportTime = -1000.0;
	bool bReloading = false;

	void FireOnce();
	void FinishReload();
};
