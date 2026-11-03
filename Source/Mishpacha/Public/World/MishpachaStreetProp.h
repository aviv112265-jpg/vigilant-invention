// MISHPACHA - street furniture a car can knock down: traffic lights, stop signs, street-name signs.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MishpachaStreetProp.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EMishpachaPropKind : uint8
{
	TrafficLight,
	StopSign,
	StreetSign,
};

UENUM()
enum class EMishpachaPropState : uint8
{
	Standing,
	Bent,
	Broken,
};

/**
 * Built from engine basic shapes. A car's bumper check (AMishpachaVehicle) calls HitByVehicle:
 * a slow hit bends the pole over, a fast one snaps it off and sends it flying (physics).
 * Only the state and the hit direction replicate; every machine animates the result itself.
 */
UCLASS()
class MISHPACHA_API AMishpachaStreetProp : public AActor
{
	GENERATED_BODY()

public:
	AMishpachaStreetProp();

	/** Server. */
	void HitByVehicle(const FVector& VehicleVelocity);

	/** Server, before BeginPlay finishes (spawn deferred): what this is, and for traffic lights which road they face (0/1, the phase). */
	void Setup(EMishpachaPropKind InKind, int32 InPhase) { Kind = InKind; Phase = InPhase; }

	bool IsStanding() const { return State == EMishpachaPropState::Standing; }

	/** Traffic lights of phase 0 (along the grid's X) and 1 (along Y) on the shared 24 s cycle: 0 red, 1 yellow, 2 green. */
	static int32 GetSignalColor(const UWorld* World, int32 InPhase);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	EMishpachaPropKind Kind = EMishpachaPropKind::StopSign;

	UPROPERTY(Replicated)
	int32 Phase = 0;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EMishpachaPropState State = EMishpachaPropState::Standing;

	UPROPERTY(Replicated)
	FVector_NetQuantize10 HitVelocity = FVector::ZeroVector;

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<USceneComponent> Root;

	/** The pole bends about this point at its foot. */
	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "Prop")
	TObjectPtr<UStaticMeshComponent> Pole;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	/** Traffic light lamps: red, yellow, green. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Lamps;

	bool bBuilt = false;
	FTimerHandle LampTimer;

	void Build();
	void UpdateLamps();
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FRotator& Rotation, UMaterialInterface* Material, const FLinearColor& Color);
};
