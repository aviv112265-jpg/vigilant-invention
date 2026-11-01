// MISHPACHA - base playable / NPC human character.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MishpachaCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class AMishpachaWeapon;
class AMishpachaVehicle;
class USiblingAbilityComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMishpachaHealthChanged, float, NewHealth, float, MaxHealth);

/**
 * Third-person character used for the three siblings, online players and (optionally) NPCs.
 * Create Blueprint children (BP_Rafi, BP_Shira, BP_Omer) to assign meshes and animation.
 */
UCLASS()
class MISHPACHA_API AMishpachaCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMishpachaCharacter();

	// ----- Input entry points (called by AMishpachaPlayerController on the owning client) -----
	void Move(const FVector2D& Axis);
	void Look(const FVector2D& Axis);
	void SetSprinting(bool bNewSprinting);
	void SetAiming(bool bNewAiming);
	void StartFire();
	void StopFire();
	void Interact();
	void UseAbility();

	// ----- Queries -----
	UFUNCTION(BlueprintPure, Category = "Health")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Health")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintPure, Category = "Family")
	FName GetSiblingId() const { return SiblingId; }

	void SetSiblingId(FName NewSiblingId) { SiblingId = NewSiblingId; }

	UFUNCTION(BlueprintPure, Category = "Abilities")
	USiblingAbilityComponent* GetAbility() const;

	UFUNCTION(BlueprintPure, Category = "Combat")
	AMishpachaWeapon* GetWeapon() const { return CurrentWeapon; }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	AMishpachaVehicle* GetCurrentVehicle() const { return CurrentVehicle; }

	UFUNCTION(BlueprintPure, Category = "Camera")
	UCameraComponent* GetFollowCamera() const { return FollowCamera; }

	// ----- Server-side helpers -----
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Combat")
	void EquipWeaponClass(TSubclassOf<AMishpachaWeapon> WeaponClass);

	/** Adds an ability component of this class unless one already exists. */
	void EnsureAbility(TSubclassOf<USiblingAbilityComponent> AbilityClass);

	/** Called by AMishpachaVehicle (server). */
	void OnEnteredVehicle(AMishpachaVehicle* Vehicle);
	void OnExitedVehicle(const FVector& ExitLocation, const FRotator& ExitRotation);

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnMishpachaHealthChanged OnHealthChanged;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float WalkSpeed = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float JogSpeed = 450.f;

	UPROPERTY(EditDefaultsOnly, Category = "Movement")
	float SprintSpeed = 700.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float DefaultArmLength = 350.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float AimArmLength = 180.f;

	UPROPERTY(EditDefaultsOnly, Category = "Health", meta = (ClampMin = "1"))
	float MaxHealth = 200.f;

	/** How long a dead body stays in the world (s). */
	UPROPERTY(EditDefaultsOnly, Category = "Health")
	float BodyLifeSpan = 30.f;

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float InteractRadius = 400.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	TSubclassOf<AMishpachaWeapon> DefaultWeaponClass;

	/** Bone or socket the weapon attaches to (UE5 Manny/Quinn: "hand_r"). */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	FName WeaponSocketName = TEXT("hand_r");

	/** "Rafi", "Shira", "Omer" for the siblings; None for online players / NPCs. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Family")
	FName SiblingId;

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 200.f;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(ReplicatedUsing = OnRep_Sprinting)
	bool bSprinting = false;

	UPROPERTY(ReplicatedUsing = OnRep_Aiming)
	bool bAiming = false;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	TObjectPtr<AMishpachaWeapon> CurrentWeapon;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentVehicle)
	TObjectPtr<AMishpachaVehicle> CurrentVehicle;

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void ServerInteract();

	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Sprinting();

	UFUNCTION()
	void OnRep_Aiming();

	UFUNCTION()
	void OnRep_CurrentWeapon();

	UFUNCTION()
	void OnRep_CurrentVehicle();

	/** Blueprint hook: play a death sound, drop loot, etc. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Health")
	void BP_OnDied();

	void Die(AController* Killer);
	void UpdateMovementSpeed();
	void UpdateAimState();
	void ApplyVehicleState();
	void AttachWeapon();
	AMishpachaVehicle* FindNearbyVehicle() const;
};
