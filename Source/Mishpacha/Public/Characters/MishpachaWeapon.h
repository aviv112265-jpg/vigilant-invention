// MISHPACHA - simple server-authoritative hitscan weapon.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MishpachaWeapon.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
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

	// ----- For the HUD (crosshair, weapon wheel) -----
	const FText& GetDisplayName() const { return DisplayName; }
	float GetSpreadDegrees() const { return SpreadDegrees; }
	float GetDamage() const { return Damage * PelletsPerShot; }
	float GetFireRate() const { return FireRate; }
	float GetRange() const { return Range; }
	int32 GetPelletsPerShot() const { return PelletsPerShot; }
	bool IsAutomatic() const { return bAutomatic; }
	bool IsReloading() const { return bReloading; }
	/** Owning client: world time of the last shot fired / last hit the server confirmed (crosshair kick, hit marker). */
	double GetLastShotTime() const { return LastLocalShotTime; }
	double GetLastHitTime() const { return LastConfirmedHitTime; }
	bool WasLastHitKill() const { return bLastHitWasKill; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	/** Gray-box gun shape in the hand (until a real weapon mesh is assigned). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Name on the weapon wheel. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FText DisplayName;

	/** Damage per pellet. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float Damage = 25.f;

	/** Shotguns fire several pellets per shot, each with its own spread. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PelletsPerShot = 1;

	/** Size (cm) of the gray-box gun shape. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	FVector BodySize = FVector(22.f, 4.f, 12.f);

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

	/** Extra spread (degrees) when an AI pulls the trigger (police, rivals). */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float AIExtraSpreadDegrees = 7.f;

	/** AI damage multiplier and minimum seconds between AI shots (gunfights last long enough to react). */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float AIDamageScale = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	float AIMinShotInterval = 0.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TSubclassOf<UDamageType> DamageTypeClass;

	/** Draw a short debug line per shot (development builds only) - handy before VFX exist. */
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	bool bDrawDebugTracer = true;

	UPROPERTY(ReplicatedUsing = OnRep_Ammo)
	int32 Ammo = 0;

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize TraceStart, FVector_NetQuantizeNormal TraceDirection);

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireEffects(FVector_NetQuantize ImpactPoint);

	/** Server -> shooter: one of your pellets hit someone (crosshair hit marker). */
	UFUNCTION(Client, Unreliable)
	void ClientConfirmHit(bool bKilled);

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
	double LastLocalShotTime = -1000.0;
	double LastConfirmedHitTime = -1000.0;
	bool bLastHitWasKill = false;
	bool bReloading = false;

	void FireOnce();
	void FinishReload();
};

/** Compact SMG: fast automatic fire, light hits, wide spread. */
UCLASS()
class MISHPACHA_API AMishpachaSMG : public AMishpachaWeapon
{
	GENERATED_BODY()

public:
	AMishpachaSMG();
};

/** Pump shotgun: 8 pellets, devastating up close, slow, short range. */
UCLASS()
class MISHPACHA_API AMishpachaShotgun : public AMishpachaWeapon
{
	GENERATED_BODY()

public:
	AMishpachaShotgun();
};

/** Assault rifle: automatic, accurate, hits hard at range. */
UCLASS()
class MISHPACHA_API AMishpachaRifle : public AMishpachaWeapon
{
	GENERATED_BODY()

public:
	AMishpachaRifle();
};
