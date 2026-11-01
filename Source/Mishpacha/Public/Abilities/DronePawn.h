// MISHPACHA - Omer's remote-controlled drone.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "DronePawn.generated.h"

class UCameraComponent;
class UFloatingPawnMovement;
class USphereComponent;
class UStaticMeshComponent;
class UOmerDroneAbility;

/**
 * Flown by the player while Omer's ability is active. Marks actors tagged "Enemy" with
 * Custom Depth (only on the pilot's screen) - add a post-process outline material that reads CustomStencil.
 * Movement is client-driven and synced to the server (fine for a scouting drone).
 */
UCLASS()
class MISHPACHA_API ADronePawn : public APawn
{
	GENERATED_BODY()

public:
	ADronePawn();

	virtual void Tick(float DeltaSeconds) override;

	/** Server: called right after spawning. */
	void InitDrone(UOmerDroneAbility* InAbility, AActor* InOperator);

	// Owning-client input
	void MoveInput(const FVector2D& Axis);
	void LookInput(const FVector2D& Axis);
	void SetVerticalInput(float Value) { VerticalInput = Value; }
	void RequestRecall();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UStaticMeshComponent> DroneMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone")
	TObjectPtr<UFloatingPawnMovement> DroneMovement;

	UPROPERTY(EditDefaultsOnly, Category = "Drone")
	float MarkRange = 5000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Drone")
	float MarkInterval = 0.5f;

	/** Beyond this distance from Omer the signal is lost and the drone returns. */
	UPROPERTY(EditDefaultsOnly, Category = "Drone")
	float MaxSignalRange = 15000.f;

	UPROPERTY(EditDefaultsOnly, Category = "Drone")
	FName EnemyTag = TEXT("Enemy");

	UPROPERTY(EditDefaultsOnly, Category = "Drone", meta = (ClampMin = "0", ClampMax = "255"))
	int32 MarkStencilValue = 1;

	UFUNCTION(Server, Reliable)
	void ServerRequestRecall();

	UFUNCTION(Server, Unreliable)
	void ServerSyncTransform(FVector_NetQuantize NewLocation, FRotator NewRotation);

	UFUNCTION(BlueprintImplementableEvent, Category = "Drone")
	void BP_OnEnemyMarked(AActor* Enemy);

private:
	TWeakObjectPtr<UOmerDroneAbility> Ability;
	TWeakObjectPtr<AActor> OperatorActor;
	TSet<TWeakObjectPtr<AActor>> MarkedActors;
	FTimerHandle MarkTimer;
	float VerticalInput = 0.f;
	float TimeSinceTransformSync = 0.f;

	void MarkEnemies();
	void RecallNow();
};
