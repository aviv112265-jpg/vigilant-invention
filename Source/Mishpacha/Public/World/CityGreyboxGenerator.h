// MISHPACHA - procedural gray-box "Tel Avir" so the game is playable in an empty level.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CityGreyboxGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UMissionDefinition;
class APawn;

/**
 * Builds a block grid (roads, sidewalks, Bauhaus-white low-rises, a glass-tower corner,
 * a beach and the sea on the west side) from engine basic shapes.
 *
 * Networking: only the integer Seed replicates. Every machine builds the same geometry
 * locally from it, so there are no per-building network actors.
 * Gameplay actors (sibling spawn points, hospital, mission markers, targets, civilians)
 * are spawned by the server only.
 *
 * You normally don't place this yourself: AMishpachaGameModeBase spawns one automatically
 * when the level has no sibling spawn points (see bAutoGreybox). Place one manually to tweak it.
 */
UCLASS()
class MISHPACHA_API ACityGreyboxGenerator : public AActor
{
	GENERATED_BODY()

public:
	ACityGreyboxGenerator();

	/** Server: builds the city and spawns gameplay actors. Safe to call more than once. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Greybox")
	void Generate();

	/** Total width of the street grid (cm). */
	float GetCityExtent() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 0 = random each session. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Seed, Category = "Greybox")
	int32 Seed = 1909;

	UPROPERTY(EditAnywhere, Category = "Greybox", meta = (ClampMin = "2", ClampMax = "16"))
	int32 BlocksX = 6;

	UPROPERTY(EditAnywhere, Category = "Greybox", meta = (ClampMin = "2", ClampMax = "16"))
	int32 BlocksY = 6;

	/** Side of one city block (cm). */
	UPROPERTY(EditAnywhere, Category = "Greybox")
	float BlockSize = 6000.f;

	UPROPERTY(EditAnywhere, Category = "Greybox")
	float RoadWidth = 1600.f;

	UPROPERTY(EditAnywhere, Category = "Greybox")
	float SidewalkHeight = 15.f;

	UPROPERTY(EditAnywhere, Category = "Greybox")
	float BeachWidth = 8000.f;

	/** Spawn sibling spawn points, hospital, targets and default missions (server). */
	UPROPERTY(EditAnywhere, Category = "Greybox|Gameplay")
	bool bSpawnGameplayActors = true;

	/** Spawn a sun + sky if the level has no directional light. */
	UPROPERTY(EditAnywhere, Category = "Greybox|Gameplay")
	bool bSpawnLightingIfMissing = true;

	UPROPERTY(EditAnywhere, Category = "Greybox|Civilians", meta = (ClampMin = "0", ClampMax = "200"))
	int32 MaxCivilians = 30;

	UPROPERTY(EditAnywhere, Category = "Greybox|Civilians")
	float CivilianSpawnRadius = 6000.f;

	UPROPERTY(EditAnywhere, Category = "Greybox|Civilians")
	float CivilianDespawnRadius = 10000.f;

	UPROPERTY(EditAnywhere, Category = "Greybox|Civilians")
	TSubclassOf<APawn> CivilianClass;

	// ----- Geometry (one instanced component per material) -----
	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> RoadsISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> SidewalksISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> BuildingsWhiteISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> BuildingsCreamISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> TowersISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> RoofDetailsISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> LaneMarksISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> SandISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> SeaISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox")
	TObjectPtr<UInstancedStaticMeshComponent> BeaconsISM;

	UFUNCTION()
	void OnRep_Seed();

private:
	/** Code-built missions; referenced here so they aren't garbage collected. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMissionDefinition>> DefaultMissions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	TArray<TWeakObjectPtr<APawn>> Civilians;
	TArray<FVector> SidewalkPoints; // local space
	FTimerHandle CivilianTimer;
	bool bGeometryBuilt = false;
	bool bGameplaySpawned = false;

	void BuildGeometry();
	void SpawnLightingIfMissing();
	void SpawnGameplayActors();
	void CreateDefaultMissions(const TMap<FName, FVector>& Targets);
	void MaintainCivilians();

	void SetupISM(UInstancedStaticMeshComponent* ISM, UStaticMesh* Mesh, UMaterialInterface* BaseMaterial, const FLinearColor& Color, bool bCollision);

	/** Local-space center of the intersection between vertical road I and horizontal road J. */
	FVector IntersectionLocal(int32 I, int32 J) const;
	FVector ToWorld(const FVector& Local) const;
};
