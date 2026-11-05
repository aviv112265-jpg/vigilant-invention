// MISHPACHA - a business Rafi can buy: earns money, and rival crews try to take it.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MishpachaBusiness.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EMishpachaBusinessState : uint8
{
	ForSale,
	Owned,
	UnderAttack,
};

/** A spot on the sidewalk with a sign: the color shows its state. UMishpachaEmpireSubsystem runs the rules. */
UCLASS()
class MISHPACHA_API AMishpachaBusiness : public AActor
{
	GENERATED_BODY()

public:
	AMishpachaBusiness();

	/** Server, spawn deferred. */
	void Setup(FName InId, const FText& InName, int32 InPrice, int32 InIncome);

	/** Server. */
	void SetState(EMishpachaBusinessState NewState, double InAttackEndTime = 0.0);
	void SetLost(bool bInLost) { bLost = bInLost; }

	FName GetBusinessId() const { return BusinessId; }
	const FText& GetDisplayName() const { return DisplayName; }
	EMishpachaBusinessState GetState() const { return State; }
	int32 GetIncome() const { return Income; }
	bool IsLost() const { return bLost; }
	double GetAttackEndTime() const { return AttackEndTime; }

	/** A business a rival crew took costs half to buy back. */
	int32 GetCurrentPrice() const { return bLost ? Price / 2 : Price; }

	static constexpr float InteractRadius = 450.f;

	/** The closest business within Radius of a point, or null. */
	static AMishpachaBusiness* FindNearest(const UWorld* World, const FVector& Location, float Radius);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "Business")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Business")
	TObjectPtr<UStaticMeshComponent> Disc;

	UPROPERTY(VisibleAnywhere, Category = "Business")
	TObjectPtr<UStaticMeshComponent> Pole;

	UPROPERTY(VisibleAnywhere, Category = "Business")
	TObjectPtr<UStaticMeshComponent> Sign;

	UPROPERTY(Replicated)
	FName BusinessId;

	UPROPERTY(Replicated)
	FText DisplayName;

	UPROPERTY(Replicated)
	int32 Price = 0;

	UPROPERTY(Replicated)
	int32 Income = 0;

	UPROPERTY(Replicated)
	bool bLost = false;

	UPROPERTY(Replicated)
	double AttackEndTime = 0.0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EMishpachaBusinessState State = EMishpachaBusinessState::ForSale;

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DiscMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SignMaterial;
};
