// MISHPACHA - procedural gray-box "Tel Avir" so the game is playable in an empty level.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/MishpachaMapTypes.h"
#include "CityGreyboxGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UMissionDefinition;
class APawn;
class AMishpachaVehicle;

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

	// ----- Map / GPS (valid on every machine once the geometry is built) -----
	bool IsGeometryBuilt() const { return bGeometryBuilt; }
	const TArray<FMishpachaMapArea>& GetMapAreas() const { return MapAreas; }
	const TArray<FMishpachaMapPlace>& GetMapPlaces() const { return MapPlaces; }

	/** Square world-space X/Y region the map texture covers (city, beach and some sea). */
	FBox2D GetMapBounds() const { return MapBounds; }

	/** Closest point on a road center line. */
	FVector SnapToRoad(const FVector& WorldLocation) const;

	/** Shortest path along the street grid (world points, on the road surface). False if there's no grid. */
	bool FindRoadRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPoints) const;

	/** Center of the intersection closest to a world point (road surface). */
	FVector GetNearestIntersection(const FVector& WorldLocation) const;

	/** Street grid: intersections (I, J) for I in 0..BlocksX, J in 0..BlocksY (world, road surface). */
	FVector GetIntersection(int32 I, int32 J) const { return ToWorld(IntersectionLocal(I, J) + FVector(0.f, 0.f, 2.f)); }
	int32 GetBlocksX() const { return BlocksX; }
	int32 GetBlocksY() const { return BlocksY; }

	/** Civilian cars driving around the players (server). */
	UPROPERTY(EditAnywhere, Category = "Greybox|Vehicles", meta = (ClampMin = "0", ClampMax = "40"))
	int32 MaxTraffic = 8;

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

	/** Cars parked along the curbs (server), plus 3 next to the family's corner. Needs a car the game can build - see AMishpachaVehicle auto setup. */
	UPROPERTY(EditAnywhere, Category = "Greybox|Vehicles", meta = (ClampMin = "0", ClampMax = "150"))
	int32 NumParkedCars = 18;

	UPROPERTY(EditAnywhere, Category = "Greybox|Vehicles")
	TSubclassOf<AMishpachaVehicle> ParkedCarClass;

	UPROPERTY(EditAnywhere, Category = "Greybox|Civilians", meta = (ClampMin = "0", ClampMax = "200"))
	int32 MaxCivilians = 12;

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

	// ----- Street details -----
	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> BalconiesISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> ACUnitsISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> TreeTrunksISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> TreeCanopiesISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> PalmTrunksISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> PalmFrondsISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> LampPostsISM;

	UPROPERTY(VisibleAnywhere, Category = "Greybox|Details")
	TObjectPtr<UInstancedStaticMeshComponent> LampHeadsISM;

	/** Balconies, AC units, street trees, beach palms and street lamps. */
	UPROPERTY(EditAnywhere, Category = "Greybox|Details")
	bool bStreetDetails = true;

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
	TArray<FMishpachaMapArea> MapAreas;
	TArray<FMishpachaMapPlace> MapPlaces;
	FBox2D MapBounds = FBox2D(ForceInit);
	FTimerHandle CivilianTimer;
	FTimerHandle TrafficTimer;
	TArray<TWeakObjectPtr<APawn>> TrafficCars;
	int32 TrafficReportCounter = 0;
	void MaintainTraffic();
	bool bGeometryBuilt = false;
	bool bGameplaySpawned = false;

	void BuildGeometry();
	void SpawnLightingIfMissing();
	void SpawnGameplayActors();
	void SpawnParkedCars();
	void CreateDefaultMissions(const TMap<FName, FVector>& Targets);
	void MaintainCivilians();

	void SetupISM(UInstancedStaticMeshComponent* ISM, UStaticMesh* Mesh, UMaterialInterface* BaseMaterial, const FLinearColor& Color, bool bCollision);

	/** Local-space center of the intersection between vertical road I and horizontal road J. */
	FVector IntersectionLocal(int32 I, int32 J) const;
	FVector ToWorld(const FVector& Local) const;

	/** Local-space map rectangle -> world-space map area. */
	void AddMapArea(const FVector& LocalCenter, float SizeX, float SizeY, EMishpachaMapAreaType Type);

	/** Where a local point lands on the road grid: the road line, which segment of it, and the point. */
	struct FRoadSnap
	{
		FVector2D Point = FVector2D::ZeroVector;
		bool bAlongY = false; // true: road of constant X (runs along Y)
		int32 Line = 0;
		int32 Segment = 0;
	};
	FRoadSnap SnapLocal(const FVector2D& Local) const;
};
