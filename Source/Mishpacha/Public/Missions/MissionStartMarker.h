// MISHPACHA - world marker that starts a mission when the right character walks/drives in.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MissionStartMarker.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;
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

	/** Sibling accent color and "WHO: job" label (replicated; the map draws these). */
	FLinearColor GetMarkerColor() const { return MarkerColor; }
	const FText& GetMarkerText() const { return MarkerText; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<USphereComponent> Trigger;

	/** Assign a mesh/material in the Blueprint (e.g. a glowing ring in the sibling's accent color). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UStaticMeshComponent> Visual;

	/** Floating "WHO: job title" label, turned toward the local camera. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mission")
	TObjectPtr<UMissionDefinition> Mission;

	/** Resolved on the server from the required sibling's accent color / name, replicated for clients. */
	UPROPERTY(ReplicatedUsing = OnRep_Appearance)
	FLinearColor MarkerColor = FLinearColor(0.9f, 0.75f, 0.1f);

	UPROPERTY(ReplicatedUsing = OnRep_Appearance)
	FText MarkerText;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DiscMaterial;

	UFUNCTION()
	void OnRep_Appearance();

	/** Server: read the sibling's color / name (the family is set up after markers spawn, so this runs a tick later). */
	void ResolveAppearance();

	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleMissionEnded(FName MissionId, bool bSuccess);

	void RefreshAvailability();
};
