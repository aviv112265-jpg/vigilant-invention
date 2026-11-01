// MISHPACHA - world marker that starts a mission when the right character walks/drives in.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MissionStartMarker.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UMissionDefinition;

UCLASS()
class MISHPACHA_API AMissionStartMarker : public AActor
{
	GENERATED_BODY()

public:
	AMissionStartMarker();

	/** For markers spawned from code (e.g. the gray-box generator). Call before FinishSpawning when possible. */
	void SetMission(UMissionDefinition* InMission);

	UMissionDefinition* GetMission() const { return Mission; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<USphereComponent> Trigger;

	/** Assign a mesh/material in the Blueprint (e.g. a glowing ring in the sibling's accent color). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UMissionDefinition> Mission;

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleMissionEnded(FName MissionId, bool bSuccess);

	void RefreshAvailability();
};
