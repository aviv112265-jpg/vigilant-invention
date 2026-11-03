// MISHPACHA - procedural gray-box "Tel Avir".
#include "World/CityGreyboxGenerator.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Characters/MishpachaCat.h"
#include "World/MishpachaStreetProp.h"
#include "Family/SiblingSpawnPoint.h"
#include "Missions/MissionDefinition.h"
#include "Missions/MissionObjectives.h"
#include "Missions/MissionStartMarker.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Vehicles/MishpachaTrafficCar.h"
#include "AI/MishpachaTrafficAI.h"
#include "GameFramework/PlayerController.h"
#include "Mishpacha.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaGreybox"

namespace GreyboxNames
{
	static const FName Rafi(TEXT("Rafi"));
	static const FName Shira(TEXT("Shira"));
	static const FName Omer(TEXT("Omer"));
	static const FName Hospital(TEXT("Hospital"));
	static const FName Market(TEXT("Market"));
	static const FName Warehouse(TEXT("Warehouse"));
	static const FName Garage(TEXT("Garage"));
	static const FName Dropoff(TEXT("Dropoff"));
}

static UInstancedStaticMeshComponent* MakeISM(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
{
	UInstancedStaticMeshComponent* ISM = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
	ISM->SetupAttachment(Parent);
	// Not Static: clients build the city after play has begun, and Static components refuse
	// SetStaticMesh at that point. (All lighting is dynamic, so this costs nothing.)
	ISM->SetMobility(EComponentMobility::Stationary);
	return ISM;
}

ACityGreyboxGenerator::ACityGreyboxGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;

	RoadsISM = MakeISM(this, Root, TEXT("Roads"));
	SidewalksISM = MakeISM(this, Root, TEXT("Sidewalks"));
	BuildingsWhiteISM = MakeISM(this, Root, TEXT("BuildingsWhite"));
	BuildingsCreamISM = MakeISM(this, Root, TEXT("BuildingsCream"));
	TowersISM = MakeISM(this, Root, TEXT("Towers"));
	RoofDetailsISM = MakeISM(this, Root, TEXT("RoofDetails"));
	LaneMarksISM = MakeISM(this, Root, TEXT("LaneMarks"));
	SandISM = MakeISM(this, Root, TEXT("Sand"));
	SeaISM = MakeISM(this, Root, TEXT("Sea"));
	BeaconsISM = MakeISM(this, Root, TEXT("Beacons"));
	BalconiesISM = MakeISM(this, Root, TEXT("Balconies"));
	ACUnitsISM = MakeISM(this, Root, TEXT("ACUnits"));
	TreeTrunksISM = MakeISM(this, Root, TEXT("TreeTrunks"));
	TreeCanopiesISM = MakeISM(this, Root, TEXT("TreeCanopies"));
	PalmTrunksISM = MakeISM(this, Root, TEXT("PalmTrunks"));
	PalmFrondsISM = MakeISM(this, Root, TEXT("PalmFronds"));
	LampPostsISM = MakeISM(this, Root, TEXT("LampPosts"));
	LampHeadsISM = MakeISM(this, Root, TEXT("LampHeads"));
	PaintBoxesISM = MakeISM(this, Root, TEXT("PaintBoxes"));
	PaintCylindersISM = MakeISM(this, Root, TEXT("PaintCylinders"));
	PaintConesISM = MakeISM(this, Root, TEXT("PaintCones"));
	PaintBlobsISM = MakeISM(this, Root, TEXT("PaintBlobs"));

	CivilianClass = AMishpachaCivilian::StaticClass();
	ParkedCarClass = AMishpachaVehicle::StaticClass();
}

void ACityGreyboxGenerator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACityGreyboxGenerator, Seed);
}

void ACityGreyboxGenerator::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Generate();
	}
	else if (!bGeometryBuilt && Seed != 0)
	{
		// Seed equal to the class default is not replicated (no OnRep), so build here.
		BuildGeometry();
	}
}

void ACityGreyboxGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CivilianTimer);
		World->GetTimerManager().ClearTimer(TrafficTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void ACityGreyboxGenerator::OnRep_Seed()
{
	// Clients: build once the server's seed has arrived.
	if (!HasAuthority() && !bGeometryBuilt)
	{
		BuildGeometry();
	}
}

void ACityGreyboxGenerator::Generate()
{
	if (!HasAuthority())
	{
		return;
	}

	if (Seed == 0)
	{
		Seed = FMath::RandRange(1, MAX_int32 - 1);
	}

	BuildGeometry();

	if (bSpawnGameplayActors && !bGameplaySpawned)
	{
		bGameplaySpawned = true;
		SpawnGameplayActors();
	}

	if (MaxCivilians > 0 && CivilianClass && !GetWorldTimerManager().IsTimerActive(CivilianTimer))
	{
		GetWorldTimerManager().SetTimer(CivilianTimer, this, &ACityGreyboxGenerator::MaintainCivilians, 2.f, true, 1.f);
	}
	if (MaxTraffic > 0 && ParkedCarClass && GetDefault<AMishpachaVehicle>()->HasVehicleSetup() && !GetWorldTimerManager().IsTimerActive(TrafficTimer))
	{
		GetWorldTimerManager().SetTimer(TrafficTimer, this, &ACityGreyboxGenerator::MaintainTraffic, 1.5f, true, 3.f);
	}
}

float ACityGreyboxGenerator::GetCityExtent() const
{
	return BlocksX * BlockSize + (BlocksX + 1) * RoadWidth;
}

FVector ACityGreyboxGenerator::IntersectionLocal(int32 I, int32 J) const
{
	const float ExtentX = BlocksX * BlockSize + (BlocksX + 1) * RoadWidth;
	const float ExtentY = BlocksY * BlockSize + (BlocksY + 1) * RoadWidth;
	const float Step = BlockSize + RoadWidth;
	return FVector(-ExtentX * 0.5f + RoadWidth * 0.5f + I * Step, -ExtentY * 0.5f + RoadWidth * 0.5f + J * Step, 0.f);
}

FVector ACityGreyboxGenerator::ToWorld(const FVector& Local) const
{
	return GetActorTransform().TransformPosition(Local);
}

// ---------------------------------------------------------------------------
// Map data + GPS routing along the street grid
// ---------------------------------------------------------------------------

void ACityGreyboxGenerator::AddMapArea(const FVector& LocalCenter, float SizeX, float SizeY, EMishpachaMapAreaType Type)
{
	const FVector A = ToWorld(LocalCenter - FVector(SizeX * 0.5f, SizeY * 0.5f, 0.f));
	const FVector B = ToWorld(LocalCenter + FVector(SizeX * 0.5f, SizeY * 0.5f, 0.f));
	FMishpachaMapArea& Area = MapAreas.AddDefaulted_GetRef();
	Area.Box = FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	Area.Type = Type;
}

ACityGreyboxGenerator::FRoadSnap ACityGreyboxGenerator::SnapLocal(const FVector2D& Local) const
{
	const float ExtentX = BlocksX * BlockSize + (BlocksX + 1) * RoadWidth;
	const float ExtentY = BlocksY * BlockSize + (BlocksY + 1) * RoadWidth;
	const float Step = BlockSize + RoadWidth;
	const float X0 = -ExtentX * 0.5f + RoadWidth * 0.5f;
	const float Y0 = -ExtentY * 0.5f + RoadWidth * 0.5f;
	const float ClampedX = FMath::Clamp<float>(Local.X, X0, X0 + BlocksX * Step);
	const float ClampedY = FMath::Clamp<float>(Local.Y, Y0, Y0 + BlocksY * Step);
	const int32 I = FMath::Clamp(FMath::RoundToInt((Local.X - X0) / Step), 0, BlocksX);
	const int32 J = FMath::Clamp(FMath::RoundToInt((Local.Y - Y0) / Step), 0, BlocksY);

	const FVector2D OnRoadAlongY(X0 + I * Step, ClampedY);
	const FVector2D OnRoadAlongX(ClampedX, Y0 + J * Step);

	FRoadSnap Snap;
	if (FVector2D::DistSquared(Local, OnRoadAlongY) <= FVector2D::DistSquared(Local, OnRoadAlongX))
	{
		Snap.Point = OnRoadAlongY;
		Snap.bAlongY = true;
		Snap.Line = I;
		Snap.Segment = FMath::Clamp(FMath::FloorToInt((ClampedY - Y0) / Step), 0, BlocksY - 1);
	}
	else
	{
		Snap.Point = OnRoadAlongX;
		Snap.bAlongY = false;
		Snap.Line = J;
		Snap.Segment = FMath::Clamp(FMath::FloorToInt((ClampedX - X0) / Step), 0, BlocksX - 1);
	}
	return Snap;
}

FVector ACityGreyboxGenerator::SnapToRoad(const FVector& WorldLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FRoadSnap Snap = SnapLocal(FVector2D(Local.X, Local.Y));
	return ToWorld(FVector(Snap.Point.X, Snap.Point.Y, 2.f));
}

FVector ACityGreyboxGenerator::GetNearestIntersection(const FVector& WorldLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float Step = BlockSize + RoadWidth;
	const FVector First = IntersectionLocal(0, 0);
	const int32 I = FMath::Clamp(FMath::RoundToInt((Local.X - First.X) / Step), 0, BlocksX);
	const int32 J = FMath::Clamp(FMath::RoundToInt((Local.Y - First.Y) / Step), 0, BlocksY);
	return ToWorld(IntersectionLocal(I, J) + FVector(0.f, 0.f, 2.f));
}

bool ACityGreyboxGenerator::FindRoadRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (BlocksX < 1 || BlocksY < 1)
	{
		return false;
	}

	const FTransform& Xform = GetActorTransform();
	const FVector LocalFrom = Xform.InverseTransformPosition(From);
	const FVector LocalTo = Xform.InverseTransformPosition(To);
	const FRoadSnap SnapA = SnapLocal(FVector2D(LocalFrom.X, LocalFrom.Y));
	const FRoadSnap SnapB = SnapLocal(FVector2D(LocalTo.X, LocalTo.Y));

	const float ExtentX = BlocksX * BlockSize + (BlocksX + 1) * RoadWidth;
	const float ExtentY = BlocksY * BlockSize + (BlocksY + 1) * RoadWidth;
	const float Step = BlockSize + RoadWidth;
	const FVector2D Origin(-ExtentX * 0.5f + RoadWidth * 0.5f, -ExtentY * 0.5f + RoadWidth * 0.5f);

	// Nodes: every intersection, then A (start) and B (end).
	const int32 NumY = BlocksY + 1;
	const int32 NumGrid = (BlocksX + 1) * NumY;
	const int32 NodeA = NumGrid;
	const int32 NodeB = NumGrid + 1;
	auto GridIndex = [NumY](int32 I, int32 J) { return I * NumY + J; };

	TArray<FVector2D> Positions;
	Positions.SetNum(NumGrid + 2);
	for (int32 I = 0; I <= BlocksX; ++I)
	{
		for (int32 J = 0; J <= BlocksY; ++J)
		{
			Positions[GridIndex(I, J)] = Origin + FVector2D(I * Step, J * Step);
		}
	}
	Positions[NodeA] = SnapA.Point;
	Positions[NodeB] = SnapB.Point;

	TArray<TArray<int32>> Links;
	Links.SetNum(NumGrid + 2);
	auto Link = [&Links](int32 U, int32 V)
	{
		Links[U].AddUnique(V);
		Links[V].AddUnique(U);
	};
	for (int32 I = 0; I <= BlocksX; ++I)
	{
		for (int32 J = 0; J <= BlocksY; ++J)
		{
			if (I < BlocksX) { Link(GridIndex(I, J), GridIndex(I + 1, J)); }
			if (J < BlocksY) { Link(GridIndex(I, J), GridIndex(I, J + 1)); }
		}
	}
	auto LinkSnap = [&](const FRoadSnap& Snap, int32 Node)
	{
		if (Snap.bAlongY)
		{
			Link(Node, GridIndex(Snap.Line, Snap.Segment));
			Link(Node, GridIndex(Snap.Line, Snap.Segment + 1));
		}
		else
		{
			Link(Node, GridIndex(Snap.Segment, Snap.Line));
			Link(Node, GridIndex(Snap.Segment + 1, Snap.Line));
		}
	};
	LinkSnap(SnapA, NodeA);
	LinkSnap(SnapB, NodeB);
	if (SnapA.bAlongY == SnapB.bAlongY && SnapA.Line == SnapB.Line && SnapA.Segment == SnapB.Segment)
	{
		Link(NodeA, NodeB); // same stretch of road
	}

	// Dijkstra (a few dozen nodes - a linear scan for the next node is plenty).
	TArray<double> Dist;
	TArray<int32> Prev;
	TArray<bool> Done;
	Dist.Init(TNumericLimits<double>::Max(), Positions.Num());
	Prev.Init(INDEX_NONE, Positions.Num());
	Done.Init(false, Positions.Num());
	Dist[NodeA] = 0.0;
	for (int32 Iteration = 0; Iteration < Positions.Num(); ++Iteration)
	{
		int32 Best = INDEX_NONE;
		for (int32 N = 0; N < Positions.Num(); ++N)
		{
			if (!Done[N] && (Best == INDEX_NONE || Dist[N] < Dist[Best]))
			{
				Best = N;
			}
		}
		if (Best == INDEX_NONE || Dist[Best] == TNumericLimits<double>::Max() || Best == NodeB)
		{
			break;
		}
		Done[Best] = true;
		for (const int32 Next : Links[Best])
		{
			const double Candidate = Dist[Best] + FVector2D::Distance(Positions[Best], Positions[Next]);
			if (Candidate < Dist[Next])
			{
				Dist[Next] = Candidate;
				Prev[Next] = Best;
			}
		}
	}
	if (Prev[NodeB] == INDEX_NONE && NodeA != NodeB && FVector2D::DistSquared(SnapA.Point, SnapB.Point) > 1.0)
	{
		return false;
	}

	TArray<int32> Path;
	for (int32 N = NodeB; N != INDEX_NONE; N = Prev[N])
	{
		Path.Insert(N, 0);
	}
	for (const int32 N : Path)
	{
		const FVector Point = ToWorld(FVector(Positions[N].X, Positions[N].Y, 2.f));
		if (OutPoints.Num() == 0 || FVector::DistSquared(OutPoints.Last(), Point) > 1.0)
		{
			OutPoints.Add(Point);
		}
	}
	return OutPoints.Num() > 0;
}

void ACityGreyboxGenerator::SetupISM(UInstancedStaticMeshComponent* ISM, UStaticMesh* Mesh, UMaterialInterface* BaseMaterial, const FLinearColor& Color, bool bCollision)
{
	if (!ISM)
	{
		return;
	}
	ISM->SetStaticMesh(Mesh);
	ISM->SetCollisionProfileName(bCollision ? FName(TEXT("BlockAll")) : FName(TEXT("NoCollision")));
	ISM->SetCastShadow(true);
	if (BaseMaterial)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		MID->SetVectorParameterValue(FName(TEXT("Color")), Color);
		ISM->SetMaterial(0, MID);
		Materials.Add(MID);
	}
}

// ---------------------------------------------------------------------------
// Geometry (runs on every machine, deterministic from Seed)
// ---------------------------------------------------------------------------

void ACityGreyboxGenerator::BuildGeometry()
{
	if (bGeometryBuilt)
	{
		return;
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Cube)
	{
		UE_LOG(LogMishpacha, Error, TEXT("CityGreyboxGenerator: /Engine/BasicShapes/Cube not found."));
		return;
	}
	bGeometryBuilt = true;

	if (bSpawnLightingIfMissing)
	{
		SpawnLightingIfMissing();
	}

	// Procedural materials from Tools/CreateMaterials.py (facade windows, asphalt, tiles, sand, sea);
	// plain colored basic shapes when they haven't been generated.
	auto Surface = [BaseMaterial](const TCHAR* Name) -> UMaterialInterface*
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Mishpacha/Materials/%s.%s"), Name, Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		return Material ? Material : BaseMaterial;
	};
	UMaterialInterface* Facade = Surface(TEXT("M_Facade"));
	const bool bProceduralFacade = Facade != BaseMaterial;

	SetupISM(RoadsISM, Cube, Surface(TEXT("M_Asphalt")), FLinearColor(0.04f, 0.04f, 0.045f), true);
	SetupISM(SidewalksISM, Cube, Surface(TEXT("M_Sidewalk")), FLinearColor(0.35f, 0.34f, 0.32f), true);
	SetupISM(BuildingsWhiteISM, Cube, Facade, FLinearColor(0.85f, 0.85f, 0.82f), true);
	SetupISM(BuildingsCreamISM, Cube, Facade, FLinearColor(0.8f, 0.72f, 0.58f), true);
	SetupISM(TowersISM, Cube, Facade, bProceduralFacade ? FLinearColor(1.f, 1.f, 1.f) : FLinearColor(0.25f, 0.33f, 0.4f), true);
	SetupISM(RoofDetailsISM, Cube, BaseMaterial, FLinearColor(0.15f, 0.15f, 0.17f), false);
	SetupISM(LaneMarksISM, Cube, BaseMaterial, FLinearColor(0.9f, 0.9f, 0.85f), false);
	SetupISM(SandISM, Cube, Surface(TEXT("M_Sand")), FLinearColor(0.76f, 0.66f, 0.45f), true);
	SetupISM(SeaISM, Cube, Surface(TEXT("M_Sea")), FLinearColor(0.02f, 0.18f, 0.35f), true);

	// Facade shader: floors are counted from the top of the sidewalk; towers get a curtain wall.
	for (UInstancedStaticMeshComponent* BuildingISM : { BuildingsWhiteISM.Get(), BuildingsCreamISM.Get(), TowersISM.Get() })
	{
		if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(BuildingISM->GetMaterial(0)))
		{
			MID->SetScalarParameterValue(FName(TEXT("GroundZ")), GetActorLocation().Z + SidewalkHeight);
			MID->SetScalarParameterValue(FName(TEXT("Tower")), BuildingISM == TowersISM ? 1.f : 0.f);
		}
	}
	SetupISM(BeaconsISM, Cylinder ? Cylinder : Cube, BaseMaterial, FLinearColor(1.f, 0.8f, 0.1f), false);

	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	SetupISM(BalconiesISM, Cube, BaseMaterial, FLinearColor(0.86f, 0.86f, 0.83f), false);
	SetupISM(ACUnitsISM, Cube, BaseMaterial, FLinearColor(0.62f, 0.63f, 0.62f), false);
	SetupISM(TreeTrunksISM, Cylinder ? Cylinder : Cube, BaseMaterial, FLinearColor(0.16f, 0.11f, 0.07f), true);
	SetupISM(TreeCanopiesISM, Sphere ? Sphere : Cube, BaseMaterial, FLinearColor(0.07f, 0.19f, 0.05f), false);
	SetupISM(PalmTrunksISM, Cylinder ? Cylinder : Cube, BaseMaterial, FLinearColor(0.33f, 0.26f, 0.17f), true);
	SetupISM(PalmFrondsISM, Sphere ? Sphere : Cube, BaseMaterial, FLinearColor(0.09f, 0.24f, 0.07f), false);
	SetupISM(LampPostsISM, Cylinder ? Cylinder : Cube, BaseMaterial, FLinearColor(0.12f, 0.12f, 0.13f), true);
	SetupISM(LampHeadsISM, Cube, Surface(TEXT("M_LampGlow")), FLinearColor::White, false);
	LampPostsISM->SetCastShadow(false);
	LampHeadsISM->SetCastShadow(false);

	// Painted street life: color (3 floats), stripes, night glow per instance.
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Paint = Surface(TEXT("M_Paint"));
	const bool bPaintPerInstance = Paint != BaseMaterial;
	SetupISM(PaintBoxesISM, Cube, Paint, FLinearColor(0.8f, 0.3f, 0.2f), true);
	SetupISM(PaintCylindersISM, Cylinder ? Cylinder : Cube, Paint, FLinearColor(0.2f, 0.4f, 0.3f), true);
	SetupISM(PaintConesISM, Cone ? Cone : Cube, Paint, FLinearColor(0.9f, 0.5f, 0.1f), false);
	SetupISM(PaintBlobsISM, Sphere ? Sphere : Cube, Paint, FLinearColor(0.8f, 0.1f, 0.5f), false);
	for (UInstancedStaticMeshComponent* PaintISM : { PaintBoxesISM.Get(), PaintCylindersISM.Get(), PaintConesISM.Get(), PaintBlobsISM.Get() })
	{
		PaintISM->NumCustomDataFloats = 5;
		PaintISM->SetCullDistances(0, 14000);
		PaintISM->SetCastShadow(PaintISM != PaintBlobsISM);
	}
	// Buildings: paint color (3 floats) + "has shops" (1).
	BuildingsWhiteISM->NumCustomDataFloats = 4;
	BuildingsCreamISM->NumCustomDataFloats = 4;
	for (UInstancedStaticMeshComponent* DetailISM : { BalconiesISM.Get(), ACUnitsISM.Get(), TreeCanopiesISM.Get(), PalmFrondsISM.Get() })
	{
		// Small things: skip them in the shadow pass and cull them early in the distance.
		DetailISM->SetCastShadow(DetailISM == TreeCanopiesISM || DetailISM == PalmFrondsISM);
		DetailISM->SetCullDistances(0, 12000);
	}

	FRandomStream Stream(Seed);

	const float ExtentX = BlocksX * BlockSize + (BlocksX + 1) * RoadWidth;
	const float ExtentY = BlocksY * BlockSize + (BlocksY + 1) * RoadWidth;
	const float MinX = -ExtentX * 0.5f;
	const float MinY = -ExtentY * 0.5f;
	const float Step = BlockSize + RoadWidth;

	// Cube = 100 cm, pivot at its center.
	auto Box = [](const FVector& Center, const FVector& SizeCm)
	{
		return FTransform(FRotator::ZeroRotator, Center, SizeCm / 100.f);
	};

	// --- Map data (paint order: sea, sand, asphalt, then blocks and their buildings) ---
	const float SeaWidth = 40000.f;
	MapAreas.Reset();
	MapPlaces.Reset();
	AddMapArea(FVector(MinX - BeachWidth - SeaWidth * 0.5f, 0.f, 0.f), SeaWidth, ExtentY + 40000.f, EMishpachaMapAreaType::Sea);
	AddMapArea(FVector(MinX - BeachWidth * 0.5f, 0.f, 0.f), BeachWidth, ExtentY + 4000.f, EMishpachaMapAreaType::Sand);
	AddMapArea(FVector::ZeroVector, ExtentX, ExtentY, EMishpachaMapAreaType::Road);
	{
		const FVector A = ToWorld(FVector(MinX - BeachWidth - 6000.f, MinY - 5000.f, 0.f));
		const FVector B = ToWorld(FVector(-MinX + 5000.f, -MinY + 5000.f, 0.f));
		const FVector2D Center((A.X + B.X) * 0.5f, (A.Y + B.Y) * 0.5f);
		const double Half = FMath::Max(FMath::Abs(B.X - A.X), FMath::Abs(B.Y - A.Y)) * 0.5;
		MapBounds = FBox2D(Center - FVector2D(Half), Center + FVector2D(Half));
	}

	// --- Asphalt base covering the whole grid (top at Z = 0) ---
	TArray<FTransform> Roads;
	Roads.Add(Box(FVector(0.f, 0.f, -50.f), FVector(ExtentX, ExtentY, 100.f)));
	RoadsISM->AddInstances(Roads, false);

	// --- Blocks: sidewalk slab + 2x2 lots of buildings ---
	TArray<FTransform> Sidewalks, White, Cream, Towers, Roof;
	TArray<FTransform> Balconies, ACUnits, TreeTrunks, TreeCanopies;
	SidewalkPoints.Reset();

	// Details use their own stream so adding them never changes the city's layout.
	FRandomStream DetailStream(Seed + 101);
	const FVector WorldOffset = GetActorLocation(); // the facade shader's window grid is in world space

	// Painted instances, gathered per mesh and added at the end with their colors.
	struct FPaintBatch
	{
		TArray<FTransform> Transforms;
		TArray<float> Data;
		void Add(const FTransform& Transform, const FLinearColor& Color, bool bStripes, float Glow)
		{
			Transforms.Add(Transform);
			Data.Append({ Color.R, Color.G, Color.B, bStripes ? 1.f : 0.f, Glow });
		}
	};
	FPaintBatch PaintBoxes, PaintCylinders, PaintCones, PaintBlobs;
	TArray<float> WhiteData, CreamData;

	// Tel Aviv: mostly white and cream Bauhaus, with pastel blocks in between.
	const FLinearColor Facades[] = {
		FLinearColor(0.86f, 0.86f, 0.83f), FLinearColor(0.88f, 0.87f, 0.84f), FLinearColor(0.84f, 0.84f, 0.8f), FLinearColor(0.82f, 0.74f, 0.6f),
		FLinearColor(0.9f, 0.63f, 0.5f), FLinearColor(0.62f, 0.8f, 0.68f), FLinearColor(0.93f, 0.84f, 0.52f), FLinearColor(0.6f, 0.75f, 0.88f),
		FLinearColor(0.9f, 0.64f, 0.7f), FLinearColor(0.76f, 0.47f, 0.34f), FLinearColor(0.73f, 0.67f, 0.86f), FLinearColor(0.95f, 0.78f, 0.55f) };
	const FLinearColor Awnings[] = {
		FLinearColor(0.75f, 0.06f, 0.05f), FLinearColor(0.04f, 0.3f, 0.65f), FLinearColor(0.05f, 0.45f, 0.22f), FLinearColor(0.95f, 0.65f, 0.04f),
		FLinearColor(0.85f, 0.3f, 0.5f), FLinearColor(0.95f, 0.4f, 0.05f), FLinearColor(0.1f, 0.55f, 0.6f), FLinearColor(0.35f, 0.15f, 0.45f) };
	auto PickColor = [&DetailStream](const FLinearColor* Colors, int32 Count) { return Colors[DetailStream.RandRange(0, Count - 1)]; };

	// Street-level shops on one building: awnings over some of M_Facade's 600 cm shop bays, and
	// the odd bougainvillea spilling over a corner.
	auto AddShopDetails = [&](const FVector& LotCenter, float SizeX, float SizeY)
	{
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const bool bWallAlongY = Side < 2;
			const float Sign = (Side % 2 == 0) ? 1.f : -1.f;
			const float WallLength = bWallAlongY ? SizeY : SizeX;
			const FVector Normal = bWallAlongY ? FVector(Sign, 0.f, 0.f) : FVector(0.f, Sign, 0.f);
			const FVector WallCenter = LotCenter + Normal * ((bWallAlongY ? SizeX : SizeY) * 0.5f);
			const float WorldAlong = bWallAlongY ? WorldOffset.Y : WorldOffset.X;
			const float WallStart = (bWallAlongY ? LotCenter.Y : LotCenter.X) - WallLength * 0.5f + WorldAlong;
			const float Yaw = Normal.Rotation().Yaw;
			for (float Bay = FMath::CeilToFloat(WallStart / 600.f) * 600.f; Bay + 600.f <= WallStart + WallLength; Bay += 600.f)
			{
				if (DetailStream.FRand() > 0.5f)
				{
					continue;
				}
				FVector Center = WallCenter + Normal * 55.f + FVector(0.f, 0.f, SidewalkHeight + 262.f);
				(bWallAlongY ? Center.Y : Center.X) = Bay + 300.f - WorldAlong;
				PaintBoxes.Add(FTransform(FRotator(-16.f, Yaw, 0.f), Center, FVector(1.15f, 5.f, 0.07f)),
					PickColor(Awnings, UE_ARRAY_COUNT(Awnings)), DetailStream.FRand() < 0.5f, 0.f);
			}
			if (DetailStream.FRand() < 0.12f)
			{
				// Bougainvillea: a magenta-pink mass over green, up the wall from the street.
				FVector Corner = WallCenter + Normal * 30.f;
				(bWallAlongY ? Corner.Y : Corner.X) += (DetailStream.FRand() < 0.5f ? -1.f : 1.f) * (WallLength * 0.5f - 120.f);
				const FRotator Flat(0.f, Yaw, 0.f);
				PaintBlobs.Add(FTransform(Flat, Corner + FVector(0.f, 0.f, SidewalkHeight + 220.f), FVector(0.7f, 2.2f, 3.6f)), FLinearColor(0.06f, 0.2f, 0.05f), false, 0.f);
				PaintBlobs.Add(FTransform(Flat, Corner + Normal * 15.f + FVector(0.f, 0.f, SidewalkHeight + 300.f), FVector(0.6f, 2.0f, 2.4f)),
					DetailStream.FRand() < 0.7f ? FLinearColor(0.75f, 0.03f, 0.35f) : FLinearColor(0.9f, 0.3f, 0.05f), false, 0.f);
			}
		}
	};

	// A kiosk with a few cafe tables under umbrellas (plazas and the beach promenade).
	auto AddKiosk = [&](const FVector& Spot, float Yaw)
	{
		const FRotator Facing(0.f, Yaw, 0.f);
		const FLinearColor Body = PickColor(Awnings, UE_ARRAY_COUNT(Awnings));
		PaintBoxes.Add(FTransform(Facing, Spot + FVector(0.f, 0.f, SidewalkHeight + 130.f), FVector(3.2f, 3.2f, 2.6f)), Body, false, 0.f);
		PaintBoxes.Add(FTransform(Facing, Spot + FVector(0.f, 0.f, SidewalkHeight + 275.f), FVector(4.2f, 4.2f, 0.3f)), FLinearColor(0.9f, 0.9f, 0.88f), false, 0.f);
		PaintBoxes.Add(FTransform(Facing, Spot + Facing.RotateVector(FVector(162.f, 0.f, 0.f)) + FVector(0.f, 0.f, SidewalkHeight + 200.f), FVector(0.05f, 2.6f, 0.5f)),
			FLinearColor(0.95f, 0.85f, 0.3f), false, 1.f);
		for (int32 Table = 0; Table < 3; ++Table)
		{
			const FVector At = Spot + Facing.RotateVector(FVector(420.f, (Table - 1) * 330.f, 0.f));
			const FLinearColor Shade = PickColor(Awnings, UE_ARRAY_COUNT(Awnings));
			PaintCylinders.Add(FTransform(FRotator::ZeroRotator, At + FVector(0.f, 0.f, SidewalkHeight + 37.f), FVector(0.8f, 0.8f, 0.04f)), FLinearColor(0.9f, 0.9f, 0.9f), false, 0.f);
			PaintCylinders.Add(FTransform(FRotator::ZeroRotator, At + FVector(0.f, 0.f, SidewalkHeight + 125.f), FVector(0.05f, 0.05f, 2.5f)), FLinearColor(0.6f, 0.6f, 0.6f), false, 0.f);
			PaintCones.Add(FTransform(FRotator::ZeroRotator, At + FVector(0.f, 0.f, SidewalkHeight + 255.f), FVector(2.6f, 2.6f, 0.5f)), Shade, true, 0.f);
		}
	};

	// Balconies and AC units on one Bauhaus building, aligned with M_Facade's window grid
	// (a window every 300 cm along the wall, a floor every 320 cm above a 115 cm base).
	auto AddFacadeDetails = [&](const FVector& LotCenter, float SizeX, float SizeY, int32 Floors)
	{
		const float BaseZ = SidewalkHeight + 115.f;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const bool bWallAlongY = Side < 2; // the wall faces +-X and runs along Y
			const float Sign = (Side % 2 == 0) ? 1.f : -1.f;
			const float WallLength = bWallAlongY ? SizeY : SizeX;
			const FVector Normal = bWallAlongY ? FVector(Sign, 0.f, 0.f) : FVector(0.f, Sign, 0.f);
			const FVector WallCenter = LotCenter + Normal * ((bWallAlongY ? SizeX : SizeY) * 0.5f);
			auto Size = [bWallAlongY](float Depth, float Along, float Tall) { return bWallAlongY ? FVector(Depth, Along, Tall) : FVector(Along, Depth, Tall); };

			// Ribbon balconies: a slab under each window row and a solid parapet, floors 1 and up.
			const bool bBalconies = Floors >= 2 && DetailStream.FRand() < 0.35f;
			if (bBalconies)
			{
				const float Length = WallLength * DetailStream.FRandRange(0.5f, 0.85f);
				for (int32 Floor = 1; Floor < Floors; ++Floor)
				{
					const float Z = BaseZ + Floor * 320.f + 70.f;
					Balconies.Add(Box(WallCenter + Normal * 65.f + FVector(0.f, 0.f, Z - 7.5f), Size(130.f, Length, 15.f)));
					Balconies.Add(Box(WallCenter + Normal * 124.f + FVector(0.f, 0.f, Z + 47.5f), Size(12.f, Length, 95.f)));
				}
			}

			// AC units under a few windows (the window centers sit at 300k + 150 in world space).
			const float WorldAlong = bWallAlongY ? WorldOffset.Y : WorldOffset.X;
			const float WallStart = (bWallAlongY ? LotCenter.Y : LotCenter.X) - WallLength * 0.5f + WorldAlong;
			const float FirstWindow = FMath::CeilToFloat((WallStart + 100.f - 150.f) / 300.f) * 300.f + 150.f;
			for (float H = FirstWindow; H < WallStart + WallLength - 100.f; H += 300.f)
			{
				const int32 ACFloors = bBalconies ? 1 : Floors; // balconies hide the upper floors' wall
				for (int32 Floor = 0; Floor < ACFloors; ++Floor)
				{
					if (DetailStream.FRand() > 0.12f)
					{
						continue;
					}
					const float LocalAlong = H - WorldAlong;
					FVector Center = WallCenter + Normal * 20.f + FVector(0.f, 0.f, BaseZ + Floor * 320.f + 45.f);
					(bWallAlongY ? Center.Y : Center.X) = LocalAlong;
					ACUnits.Add(Box(Center, Size(40.f, 85.f, 55.f)));
				}
			}
		}
	};

	for (int32 BX = 0; BX < BlocksX; ++BX)
	{
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			const float X0 = MinX + RoadWidth + BX * Step;
			const float Y0 = MinY + RoadWidth + BY * Step;
			const FVector BlockCenter(X0 + BlockSize * 0.5f, Y0 + BlockSize * 0.5f, 0.f);

			Sidewalks.Add(Box(BlockCenter + FVector(0.f, 0.f, SidewalkHeight * 0.5f), FVector(BlockSize, BlockSize, SidewalkHeight)));
			AddMapArea(BlockCenter, BlockSize, BlockSize, EMishpachaMapAreaType::Block);

			// Sidewalk walking points (ring just inside the curb), used for spawning pedestrians.
			const float Inset = 180.f;
			for (float T = Inset; T <= BlockSize - Inset; T += 1500.f)
			{
				SidewalkPoints.Add(FVector(X0 + T, Y0 + Inset, SidewalkHeight));
				SidewalkPoints.Add(FVector(X0 + T, Y0 + BlockSize - Inset, SidewalkHeight));
				SidewalkPoints.Add(FVector(X0 + Inset, Y0 + T, SidewalkHeight));
				SidewalkPoints.Add(FVector(X0 + BlockSize - Inset, Y0 + T, SidewalkHeight));
			}

			// "Emek HaCloud": glass towers in the north-east corner of the grid.
			const bool bTowerDistrict = (BX >= BlocksX - 2) && (BY >= BlocksY - 2);
			const float Lot = BlockSize * 0.5f;

			for (int32 LX = 0; LX < 2; ++LX)
			{
				for (int32 LY = 0; LY < 2; ++LY)
				{
					const bool bEmptyLot = Stream.FRand() < 0.08f; // plaza / parking lot
					const float Setback = Stream.FRandRange(320.f, 520.f);
					const float SizeX = Lot - 2.f * Setback;
					const float SizeY = Lot - 2.f * Stream.FRandRange(320.f, 520.f);
					const FVector LotCenter(X0 + Lot * (LX + 0.5f), Y0 + Lot * (LY + 0.5f), 0.f);
					if (bEmptyLot)
					{
						AddMapArea(LotCenter, Lot - 400.f, Lot - 400.f, EMishpachaMapAreaType::Plaza);
						if (bStreetDetails && bPaintPerInstance)
						{
							AddKiosk(LotCenter, DetailStream.RandRange(0, 3) * 90.f);
						}
						continue;
					}

					float Height;
					if (bTowerDistrict)
					{
						Height = Stream.FRandRange(6000.f, 15000.f);
						Towers.Add(Box(LotCenter + FVector(0.f, 0.f, SidewalkHeight + Height * 0.5f), FVector(SizeX, SizeY, Height)));
						AddMapArea(LotCenter, SizeX, SizeY, EMishpachaMapAreaType::Tower);
						continue;
					}

					// Bauhaus low-rises: 2-5 floors on "pilotis", the odd 8-12 floor block.
					const int32 Floors = (Stream.FRand() < 0.1f) ? Stream.RandRange(8, 12) : Stream.RandRange(2, 5);
					Height = Floors * 320.f + 100.f;
					const FTransform Building = Box(LotCenter + FVector(0.f, 0.f, SidewalkHeight + Height * 0.5f), FVector(SizeX, SizeY, Height));
					AddMapArea(LotCenter, SizeX, SizeY, EMishpachaMapAreaType::Building);
					const bool bShops = bPaintPerInstance && DetailStream.FRand() < 0.65f;
					const FLinearColor FacadePaint = Facades[DetailStream.RandRange(0, UE_ARRAY_COUNT(Facades) - 1)];
					if (Stream.FRand() < 0.6f)
					{
						White.Add(Building);
						WhiteData.Append({ FacadePaint.R, FacadePaint.G, FacadePaint.B, bShops ? 1.f : 0.f });
					}
					else
					{
						Cream.Add(Building);
						CreamData.Append({ FacadePaint.R, FacadePaint.G, FacadePaint.B, bShops ? 1.f : 0.f });
					}
					if (bShops && bStreetDetails)
					{
						AddShopDetails(LotCenter, SizeX, SizeY);
					}

					// Solar water heaters ("dudei shemesh") on the roof.
					const int32 Heaters = Stream.RandRange(1, 3);
					for (int32 H = 0; H < Heaters; ++H)
					{
						const FVector RoofSpot = LotCenter + FVector(
							Stream.FRandRange(-SizeX * 0.35f, SizeX * 0.35f),
							Stream.FRandRange(-SizeY * 0.35f, SizeY * 0.35f),
							SidewalkHeight + Height + 60.f);
						Roof.Add(Box(RoofSpot, FVector(200.f, 120.f, 120.f)));
					}

					if (bStreetDetails)
					{
						AddFacadeDetails(LotCenter, SizeX, SizeY, Floors);
					}
				}
			}

			// Street trees along the block's sidewalk, clear of the corners.
			if (bStreetDetails)
			{
				const float TreeInset = 200.f;
				for (float T = 800.f; T <= BlockSize - 800.f; T += 1200.f)
				{
					const FVector Spots[] = {
						FVector(X0 + T, Y0 + TreeInset, 0.f), FVector(X0 + T, Y0 + BlockSize - TreeInset, 0.f),
						FVector(X0 + TreeInset, Y0 + T, 0.f), FVector(X0 + BlockSize - TreeInset, Y0 + T, 0.f) };
					for (const FVector& Spot : Spots)
					{
						if (DetailStream.FRand() < 0.3f)
						{
							// No tree here: a bench facing the street and a bin, now and then.
							if (bPaintPerInstance && DetailStream.FRand() < 0.6f)
							{
								const bool bAlongX = FMath::IsNearlyEqual(Spot.Y, Y0 + TreeInset) || FMath::IsNearlyEqual(Spot.Y, Y0 + BlockSize - TreeInset);
								const FVector Inward = bAlongX ? FVector(0.f, Spot.Y < Y0 + BlockSize * 0.5f ? 1.f : -1.f, 0.f) : FVector(Spot.X < X0 + BlockSize * 0.5f ? 1.f : -1.f, 0.f, 0.f);
								const FRotator Facing = (-Inward).Rotation();
								const FVector Seat = Spot + Inward * 90.f;
								const FLinearColor Wood = DetailStream.FRand() < 0.5f ? FLinearColor(0.35f, 0.18f, 0.08f) : FLinearColor(0.05f, 0.25f, 0.12f);
								PaintBoxes.Add(FTransform(Facing, Seat + FVector(0.f, 0.f, SidewalkHeight + 45.f), FVector(0.5f, 1.8f, 0.08f)), Wood, false, 0.f);
								PaintBoxes.Add(FTransform(Facing, Seat + Inward * 24.f + FVector(0.f, 0.f, SidewalkHeight + 75.f), FVector(0.06f, 1.8f, 0.4f)), Wood, false, 0.f);
								PaintBoxes.Add(FTransform(Facing, Seat + FVector(0.f, 0.f, SidewalkHeight + 22.f), FVector(0.4f, 1.6f, 0.44f)), FLinearColor(0.08f, 0.08f, 0.08f), false, 0.f);
								PaintCylinders.Add(FTransform(FRotator::ZeroRotator, Seat + Facing.RotateVector(FVector(0.f, 130.f, 0.f)) + FVector(0.f, 0.f, SidewalkHeight + 45.f), FVector(0.5f, 0.5f, 0.9f)),
									FLinearColor(0.15f, 0.3f, 0.15f), false, 0.f);
							}
							continue;
						}
						const float Scale = DetailStream.FRandRange(0.85f, 1.2f);
						TreeTrunks.Add(FTransform(FRotator::ZeroRotator, Spot + FVector(0.f, 0.f, SidewalkHeight + 150.f * Scale), FVector(0.22f, 0.22f, 3.f * Scale)));
						TreeCanopies.Add(FTransform(FRotator(0.f, DetailStream.FRandRange(0.f, 360.f), 0.f), Spot + FVector(0.f, 0.f, SidewalkHeight + 390.f * Scale),
							FVector(2.4f, 2.1f, 1.9f) * Scale));
					}
				}
			}
		}
	}
	BalconiesISM->AddInstances(Balconies, false);
	ACUnitsISM->AddInstances(ACUnits, false);
	TreeTrunksISM->AddInstances(TreeTrunks, false);
	TreeCanopiesISM->AddInstances(TreeCanopies, false);

	SidewalksISM->AddInstances(Sidewalks, false);
	BuildingsWhiteISM->AddInstances(White, false);
	BuildingsCreamISM->AddInstances(Cream, false);
	if (bPaintPerInstance)
	{
		for (int32 Index = 0; Index < White.Num(); ++Index)
		{
			BuildingsWhiteISM->SetCustomData(Index, MakeArrayView(WhiteData.GetData() + Index * 4, 4), false);
		}
		for (int32 Index = 0; Index < Cream.Num(); ++Index)
		{
			BuildingsCreamISM->SetCustomData(Index, MakeArrayView(CreamData.GetData() + Index * 4, 4), false);
		}
	}
	TowersISM->AddInstances(Towers, false);
	RoofDetailsISM->AddInstances(Roof, false);

	// --- Dashed center lines ---
	TArray<FTransform> Marks;
	for (int32 I = 0; I <= BlocksX; ++I)
	{
		const float RoadX = MinX + RoadWidth * 0.5f + I * Step;
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			const float Y0 = MinY + RoadWidth + BY * Step;
			for (float T = 400.f; T < BlockSize - 200.f; T += 900.f)
			{
				Marks.Add(Box(FVector(RoadX, Y0 + T, 1.f), FVector(15.f, 450.f, 2.f)));
			}
		}
	}
	for (int32 J = 0; J <= BlocksY; ++J)
	{
		const float RoadY = MinY + RoadWidth * 0.5f + J * Step;
		for (int32 BX = 0; BX < BlocksX; ++BX)
		{
			const float X0 = MinX + RoadWidth + BX * Step;
			for (float T = 400.f; T < BlockSize - 200.f; T += 900.f)
			{
				Marks.Add(Box(FVector(X0 + T, RoadY, 1.f), FVector(450.f, 15.f, 2.f)));
			}
		}
	}
	LaneMarksISM->AddInstances(Marks, false);

	// --- Street lamps: both curbs of every road, a quarter block apart, clear of the intersections.
	// M_Asphalt / M_Sidewalk paint their light pools from the same numbers (set on their MIDs below).
	{
		const float LampSpacing = Step * 0.25f;
		const float LampPhase = Step * 0.125f;
		const float PostOffset = RoadWidth * 0.5f + 60.f;   // on the sidewalk, just past the curb
		const float LightOffset = PostOffset - 100.f;       // the head leans over the road
		const FVector2D GridOrigin(MinX + RoadWidth * 0.5f, MinY + RoadWidth * 0.5f); // first intersection (local)

		if (bStreetDetails)
		{
			TArray<FTransform> Posts, Heads;
			auto AddLamp = [&](const FVector& Base, const FVector& TowardRoad)
			{
				Posts.Add(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, SidewalkHeight + 300.f), FVector(0.12f, 0.12f, 6.f)));
				const FVector Head = Base + TowardRoad * 100.f + FVector(0.f, 0.f, SidewalkHeight + 590.f);
				Heads.Add(FTransform(TowardRoad.Rotation(), Head, FVector(0.7f, 0.32f, 0.14f)));
			};
			for (int32 Line = 0; Line <= FMath::Max(BlocksX, BlocksY); ++Line)
			{
				for (int32 Segment = 0; Segment < FMath::Max(BlocksX, BlocksY); ++Segment)
				{
					for (int32 K = 0; K < 4; ++K)
					{
						const float Along = LampPhase + K * LampSpacing; // from the segment's intersection
						if (Line <= BlocksX && Segment < BlocksY)
						{
							const float X = GridOrigin.X + Line * Step;
							const float Y = GridOrigin.Y + Segment * Step + Along;
							AddLamp(FVector(X + PostOffset, Y, 0.f), FVector(-1.f, 0.f, 0.f));
							AddLamp(FVector(X - PostOffset, Y, 0.f), FVector(1.f, 0.f, 0.f));
						}
						if (Line <= BlocksY && Segment < BlocksX)
						{
							const float X = GridOrigin.X + Segment * Step + Along;
							const float Y = GridOrigin.Y + Line * Step;
							AddLamp(FVector(X, Y + PostOffset, 0.f), FVector(0.f, -1.f, 0.f));
							AddLamp(FVector(X, Y - PostOffset, 0.f), FVector(0.f, 1.f, 0.f));
						}
					}
				}
			}
			LampPostsISM->AddInstances(Posts, false);
			LampHeadsISM->AddInstances(Heads, false);
		}

		const FVector WorldGridOrigin = ToWorld(FVector(GridOrigin.X, GridOrigin.Y, 0.f));
		for (UInstancedStaticMeshComponent* GroundISM : { RoadsISM.Get(), SidewalksISM.Get() })
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(GroundISM->GetMaterial(0)))
			{
				MID->SetVectorParameterValue(FName(TEXT("GridOrigin")), FLinearColor(WorldGridOrigin.X, WorldGridOrigin.Y, 0.f, 0.f));
				MID->SetScalarParameterValue(FName(TEXT("GridStep")), Step);
				MID->SetScalarParameterValue(FName(TEXT("LampOffset")), bStreetDetails ? LightOffset : 1.0e6f);
				MID->SetScalarParameterValue(FName(TEXT("LampSpacing")), LampSpacing);
				MID->SetScalarParameterValue(FName(TEXT("LampPhase")), LampPhase);
			}
		}
	}

	// --- Beach and sea on the west (negative X), like the real coast ---
	TArray<FTransform> Sand, Sea;
	Sand.Add(Box(FVector(MinX - BeachWidth * 0.5f, 0.f, -60.f), FVector(BeachWidth, ExtentY + 4000.f, 100.f)));
	Sea.Add(Box(FVector(MinX - BeachWidth - SeaWidth * 0.5f, 0.f, -150.f), FVector(SeaWidth, ExtentY + 40000.f, 100.f)));
	SandISM->AddInstances(Sand, false);
	SeaISM->AddInstances(Sea, false);

	// --- Palms along the beach promenade (sand top at Z = -10) ---
	if (bStreetDetails)
	{
		TArray<FTransform> PalmTrunks, PalmFronds;
		for (float Y = MinY; Y <= -MinY; Y += 1400.f)
		{
			const FVector Base(MinX - 600.f + DetailStream.FRandRange(-150.f, 150.f), Y + DetailStream.FRandRange(-200.f, 200.f), -10.f);
			const float Height = DetailStream.FRandRange(650.f, 900.f);
			const float Lean = DetailStream.FRandRange(-6.f, 6.f);
			const FRotator Tilt(Lean, 0.f, 0.f);
			PalmTrunks.Add(FTransform(Tilt, Base + Tilt.RotateVector(FVector(0.f, 0.f, Height * 0.5f)), FVector(0.28f, 0.28f, Height / 100.f)));
			const FVector Crown = Base + Tilt.RotateVector(FVector(0.f, 0.f, Height));
			const float Spin = DetailStream.FRandRange(0.f, 60.f);
			for (int32 Frond = 0; Frond < 3; ++Frond)
			{
				// Three long, flat ellipsoids crossed into a star.
				PalmFronds.Add(FTransform(FRotator(DetailStream.FRandRange(-12.f, 12.f), Spin + Frond * 60.f, 0.f), Crown, FVector(4.4f, 1.1f, 0.35f)));
			}
		}
		PalmTrunksISM->AddInstances(PalmTrunks, false);
		PalmFrondsISM->AddInstances(PalmFronds, false);

		if (bPaintPerInstance)
		{
			// Beach: rows of striped umbrellas with sunbeds, lifeguard towers, kiosks on the promenade.
			const FLinearColor Umbrellas[] = { FLinearColor(0.85f, 0.08f, 0.05f), FLinearColor(0.05f, 0.35f, 0.75f), FLinearColor(0.95f, 0.7f, 0.05f),
				FLinearColor(0.05f, 0.55f, 0.3f), FLinearColor(0.9f, 0.4f, 0.6f), FLinearColor(0.95f, 0.45f, 0.05f) };
			for (float Y = MinY + 600.f; Y <= -MinY - 600.f; Y += 520.f)
			{
				for (int32 Row = 0; Row < 2; ++Row)
				{
					if (DetailStream.FRand() < 0.35f)
					{
						continue;
					}
					const FVector Base(MinX - 2600.f - Row * 1500.f + DetailStream.FRandRange(-200.f, 200.f), Y + DetailStream.FRandRange(-120.f, 120.f), -10.f);
					PaintCylinders.Add(FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, 120.f), FVector(0.05f, 0.05f, 2.4f)), FLinearColor(0.85f, 0.85f, 0.85f), false, 0.f);
					PaintCones.Add(FTransform(FRotator(DetailStream.FRandRange(-8.f, 8.f), 0.f, 0.f), Base + FVector(0.f, 0.f, 235.f), FVector(2.8f, 2.8f, 0.55f)),
						Umbrellas[DetailStream.RandRange(0, UE_ARRAY_COUNT(Umbrellas) - 1)], DetailStream.FRand() < 0.6f, 0.f);
					for (const float Side : { -1.f, 1.f })
					{
						PaintBoxes.Add(FTransform(FRotator(0.f, 90.f, 0.f), Base + FVector(-60.f, Side * 80.f, 20.f), FVector(1.9f, 0.62f, 0.12f)),
							DetailStream.FRand() < 0.5f ? FLinearColor(0.9f, 0.9f, 0.88f) : FLinearColor(0.05f, 0.3f, 0.6f), false, 0.f);
					}
				}
			}
			for (float Y = MinY + 3000.f; Y <= -MinY - 3000.f; Y += 9000.f)
			{
				const FVector Tower(MinX - 5200.f, Y, -10.f);
				for (const FVector2D Leg : { FVector2D(-90.f, -90.f), FVector2D(90.f, -90.f), FVector2D(-90.f, 90.f), FVector2D(90.f, 90.f) })
				{
					PaintBoxes.Add(FTransform(FRotator::ZeroRotator, Tower + FVector(Leg.X, Leg.Y, 150.f), FVector(0.12f, 0.12f, 3.f)), FLinearColor(0.85f, 0.85f, 0.85f), false, 0.f);
				}
				PaintBoxes.Add(FTransform(FRotator::ZeroRotator, Tower + FVector(0.f, 0.f, 400.f), FVector(2.6f, 2.6f, 2.f)), FLinearColor(0.9f, 0.9f, 0.88f), false, 0.f);
				PaintBoxes.Add(FTransform(FRotator::ZeroRotator, Tower + FVector(0.f, 0.f, 520.f), FVector(3.2f, 3.2f, 0.3f)), FLinearColor(0.8f, 0.06f, 0.04f), false, 0.f);
				AddKiosk(FVector(MinX - 1400.f, Y + 4500.f, -10.f - SidewalkHeight), 180.f);
			}
		}
	}

	PaintBoxesISM->AddInstances(PaintBoxes.Transforms, false);
	PaintCylindersISM->AddInstances(PaintCylinders.Transforms, false);
	PaintConesISM->AddInstances(PaintCones.Transforms, false);
	PaintBlobsISM->AddInstances(PaintBlobs.Transforms, false);
	for (const TPair<UInstancedStaticMeshComponent*, const FPaintBatch*>& Pair : { TPair<UInstancedStaticMeshComponent*, const FPaintBatch*>(PaintBoxesISM, &PaintBoxes),
		TPair<UInstancedStaticMeshComponent*, const FPaintBatch*>(PaintCylindersISM, &PaintCylinders), TPair<UInstancedStaticMeshComponent*, const FPaintBatch*>(PaintConesISM, &PaintCones),
		TPair<UInstancedStaticMeshComponent*, const FPaintBatch*>(PaintBlobsISM, &PaintBlobs) })
	{
		for (int32 Index = 0; Index < Pair.Value->Transforms.Num(); ++Index)
		{
			Pair.Key->SetCustomData(Index, MakeArrayView(Pair.Value->Data.GetData() + Index * 5, 5), false);
		}
		Pair.Key->MarkRenderStateDirty();
	}

	// --- Destination beacons (tall yellow pillars over mission targets) ---
	TArray<FTransform> Beacons;
	const FIntPoint BeaconCells[] = { FIntPoint(1, 1), FIntPoint(BlocksX - 1, 2), FIntPoint(BlocksX - 2, BlocksY - 1), FIntPoint(1, BlocksY - 2), FIntPoint(1, BlocksY - 1) };
	for (const FIntPoint& Cell : BeaconCells)
	{
		const FVector Spot = IntersectionLocal(Cell.X, Cell.Y);
		Beacons.Add(FTransform(FRotator::ZeroRotator, Spot + FVector(0.f, 0.f, 1500.f), FVector(0.5f, 0.5f, 30.f)));
	}
	BeaconsISM->AddInstances(Beacons, false);

	// --- Place names for the full map (same cells as the beacons, plus the family home) ---
	const TPair<FIntPoint, FText> Places[] = {
		TPair<FIntPoint, FText>(FIntPoint(1, 1), LOCTEXT("PlaceMarket", "Old Yafeh Market")),
		TPair<FIntPoint, FText>(FIntPoint(BlocksX - 1, 2), LOCTEXT("PlaceWarehouse", "Evidence Warehouse")),
		TPair<FIntPoint, FText>(FIntPoint(BlocksX - 2, BlocksY - 1), LOCTEXT("PlaceGarage", "Moshe's Garage")),
		TPair<FIntPoint, FText>(FIntPoint(1, BlocksY - 2), LOCTEXT("PlaceDropoff", "Family Drop-off")),
		TPair<FIntPoint, FText>(FIntPoint(1, BlocksY - 1), LOCTEXT("PlaceHospital", "Hospital")),
		TPair<FIntPoint, FText>(FIntPoint(BlocksX / 2, BlocksY / 2), LOCTEXT("PlaceHome", "Sela Family")),
	};
	for (const TPair<FIntPoint, FText>& Place : Places)
	{
		FMishpachaMapPlace& MapPlace = MapPlaces.AddDefaulted_GetRef();
		MapPlace.Location = ToWorld(IntersectionLocal(Place.Key.X, Place.Key.Y));
		MapPlace.Label = Place.Value;
	}

	UE_LOG(LogMishpacha, Log, TEXT("CityGreyboxGenerator: built %d buildings, %d towers (seed %d)."), White.Num() + Cream.Num(), Towers.Num(), Seed);
}

void ACityGreyboxGenerator::SpawnLightingIfMissing()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (TActorIterator<ADirectionalLight>(World))
	{
		return; // the level already has a sun
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0.f, 0.f, 5000.f), FRotator(-42.f, 35.f, 0.f), Params))
	{
		if (UDirectionalLightComponent* SunComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunComponent->SetMobility(EComponentMobility::Movable);
			SunComponent->SetIntensity(8.f);
			SunComponent->SetAtmosphereSunLight(true);
		}
	}
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.f, 0.f, 3000.f), FRotator::ZeroRotator, Params))
	{
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			SkyComponent->SetMobility(EComponentMobility::Movable);
			// One capture of the static sky; real-time capture re-renders it every frame.
			SkyComponent->bRealTimeCapture = false;
			SkyComponent->RecaptureSky();
		}
	}
}

// ---------------------------------------------------------------------------
// Gameplay actors (server)
// ---------------------------------------------------------------------------

void ACityGreyboxGenerator::SpawnGameplayActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	// --- Sibling homes: the central intersection ---
	const int32 MidI = BlocksX / 2;
	const int32 MidJ = BlocksY / 2;
	const FVector Center = IntersectionLocal(MidI, MidJ);
	struct FHome { FName Id; FVector Offset; float Yaw; };
	const FHome Homes[] = {
		{ GreyboxNames::Rafi, FVector(-450.f, 0.f, 0.f), 0.f },
		{ GreyboxNames::Shira, FVector(0.f, 450.f, 0.f), -90.f },
		{ GreyboxNames::Omer, FVector(450.f, 0.f, 0.f), 180.f },
	};
	for (const FHome& Home : Homes)
	{
		bool bExists = false;
		for (TActorIterator<ASiblingSpawnPoint> It(World); It; ++It)
		{
			if (It->SiblingId == Home.Id)
			{
				bExists = true;
				break;
			}
		}
		if (bExists)
		{
			continue;
		}

		const FTransform SpawnTransform(FRotator(0.f, GetActorRotation().Yaw + Home.Yaw, 0.f), ToWorld(Center + Home.Offset + FVector(0.f, 0.f, 120.f)));
		if (ASiblingSpawnPoint* Point = World->SpawnActorDeferred<ASiblingSpawnPoint>(ASiblingSpawnPoint::StaticClass(), SpawnTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Point->SiblingId = Home.Id;
			Point->FinishSpawning(SpawnTransform);
		}
	}

	// --- Player starts for online free roam, on a ring around the family's intersection ---
	// (the level's own PlayerStart usually ends up under the runtime city).
	for (int32 Index = 0; Index < 8; ++Index)
	{
		const float Angle = 2.f * PI * (Index + 0.5f) / 8.f;
		const FVector Spot = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 650.f + FVector(0.f, 0.f, 120.f);
		if (APlayerStart* Start = World->SpawnActor<APlayerStart>(ToWorld(Spot), FRotator(0.f, FMath::RadiansToDegrees(Angle), 0.f), Params))
		{
			Start->Tags.AddUnique(FName(TEXT("Greybox")));
		}
	}

	// --- Named places (target points with tags; missions and respawn find them by tag) ---
	TMap<FName, FVector> Targets;
	auto AddPlace = [&](FName Tag, int32 I, int32 J)
	{
		const FVector WorldSpot = ToWorld(IntersectionLocal(I, J) + FVector(0.f, 0.f, 100.f));
		Targets.Add(Tag, WorldSpot);
		if (ATargetPoint* Point = World->SpawnActor<ATargetPoint>(WorldSpot, FRotator::ZeroRotator, Params))
		{
			Point->Tags.AddUnique(Tag);
		}
	};
	// Same cells as the beacons built in BuildGeometry.
	AddPlace(GreyboxNames::Market, 1, 1);                       // "Old Yafeh" market, near the beach
	AddPlace(GreyboxNames::Warehouse, BlocksX - 1, 2);          // police evidence warehouse
	AddPlace(GreyboxNames::Garage, BlocksX - 2, BlocksY - 1);   // Moshe's garage
	AddPlace(GreyboxNames::Dropoff, 1, BlocksY - 2);            // family drop-off
	AddPlace(GreyboxNames::Hospital, 1, BlocksY - 1);           // hospital (respawn point)

	// --- Parked cars (before the missions: "get a car" steps only exist when there are cars) ---
	SpawnParkedCars();
	if (bStreetProps)
	{
		SpawnStreetProps();
	}

	// --- Default missions, unless the level already has its own markers ---
	const bool bHasMarkers = static_cast<bool>(TActorIterator<AMissionStartMarker>(World));
	if (!bHasMarkers)
	{
		CreateDefaultMissions(Targets);
	}
}

void ACityGreyboxGenerator::SpawnStreetProps()
{
	UWorld* World = GetWorld();
	FRandomStream Stream(Seed + 303);
	const float Corner = RoadWidth * 0.5f + 90.f; // on the sidewalk, just past the curb
	int32 Count = 0;

	auto Spawn = [&](EMishpachaPropKind Kind, int32 I, int32 J, float SX, float SY, float Yaw, int32 Phase)
	{
		const FVector Local = IntersectionLocal(I, J) + FVector(SX * Corner, SY * Corner, SidewalkHeight);
		const FTransform Transform(FRotator(0.f, Yaw, 0.f), ToWorld(Local));
		AMishpachaStreetProp* Prop = World->SpawnActorDeferred<AMishpachaStreetProp>(AMishpachaStreetProp::StaticClass(), Transform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Prop)
		{
			Prop->Setup(Kind, Phase);
			Prop->FinishSpawning(Transform);
			if (Kind == EMishpachaPropKind::TrafficLight)
			{
				Signals.Add(FIntVector(I, J, Phase), Prop);
			}
			++Count;
		}
	};

	for (int32 I = 0; I <= BlocksX; ++I)
	{
		for (int32 J = 0; J <= BlocksY; ++J)
		{
			const bool bInner = I > 0 && J > 0 && I < BlocksX && J < BlocksY;
			if (bInner && (I + J) % 2 == 0)
			{
				// Traffic lights for both streets (Israel drives on the right: the light stands on the far right corner).
				Spawn(EMishpachaPropKind::TrafficLight, I, J, 1.f, -1.f, 180.f, 0);   // traffic going +X
				Spawn(EMishpachaPropKind::TrafficLight, I, J, -1.f, 1.f, 0.f, 0);     // going -X
				Spawn(EMishpachaPropKind::TrafficLight, I, J, 1.f, 1.f, -90.f, 1);    // going +Y
				Spawn(EMishpachaPropKind::TrafficLight, I, J, -1.f, -1.f, 90.f, 1);   // going -Y
			}
			else if (Stream.FRand() < 0.7f)
			{
				const float SX = Stream.FRand() < 0.5f ? -1.f : 1.f;
				const float SY = Stream.FRand() < 0.5f ? -1.f : 1.f;
				Spawn(EMishpachaPropKind::StopSign, I, J, SX, SY, Stream.RandRange(0, 3) * 90.f, 0);
				Spawn(EMishpachaPropKind::StreetSign, I, J, -SX, -SY, 45.f, 0);
			}
		}
	}
	UE_LOG(LogMishpacha, Log, TEXT("CityGreyboxGenerator: %d street props."), Count);
}

int32 ACityGreyboxGenerator::GetSignalColor(const FIntPoint& Node, bool bAlongX) const
{
	// Lights of phase 0 face the traffic along X, phase 1 along Y; once they're all knocked down, nobody waits.
	TArray<TWeakObjectPtr<AMishpachaStreetProp>> Lights;
	Signals.MultiFind(FIntVector(Node.X, Node.Y, bAlongX ? 0 : 1), Lights);
	for (const TWeakObjectPtr<AMishpachaStreetProp>& Light : Lights)
	{
		if (Light.IsValid() && Light->IsStanding())
		{
			return AMishpachaStreetProp::GetSignalColor(GetWorld(), bAlongX ? 0 : 1);
		}
	}
	return -1;
}

void ACityGreyboxGenerator::SpawnParkedCars()
{
	UWorld* World = GetWorld();
	const AMishpachaVehicle* CarDefaults = ParkedCarClass ? ParkedCarClass->GetDefaultObject<AMishpachaVehicle>() : nullptr;
	if (!World || NumParkedCars <= 0 || !CarDefaults)
	{
		return;
	}
	if (!CarDefaults->HasVehicleSetup())
	{
		UE_LOG(LogMishpacha, Log, TEXT("CityGreyboxGenerator: no parked cars - add the Vehicle content pack (or set Parked Car Class to your BP_Car)."));
		return;
	}

	const float ExtentX = BlocksX * BlockSize + (BlocksX + 1) * RoadWidth;
	const float ExtentY = BlocksY * BlockSize + (BlocksY + 1) * RoadWidth;
	const float MinX = -ExtentX * 0.5f;
	const float MinY = -ExtentY * 0.5f;
	const float Step = BlockSize + RoadWidth;
	const float CurbOffset = RoadWidth * 0.5f - 240.f; // car center, just off the curb
	const FVector HomeCenter = IntersectionLocal(BlocksX / 2, BlocksY / 2);

	// Every curb spot: two per side of every block-long stretch of road, kept away from the intersections.
	struct FSpot { FVector Location; float Yaw; };
	TArray<FSpot> Spots;
	for (int32 I = 0; I <= BlocksX; ++I)
	{
		const float RoadX = MinX + RoadWidth * 0.5f + I * Step;
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			const float Y0 = MinY + RoadWidth + BY * Step;
			for (const float T : { 0.3f, 0.7f })
			{
				// Right-hand traffic: the car on the +X curb faces +Y, the one on the -X curb faces -Y.
				Spots.Add({ FVector(RoadX + CurbOffset, Y0 + BlockSize * T, 0.f), 90.f });
				Spots.Add({ FVector(RoadX - CurbOffset, Y0 + BlockSize * T, 0.f), -90.f });
			}
		}
	}
	for (int32 J = 0; J <= BlocksY; ++J)
	{
		const float RoadY = MinY + RoadWidth * 0.5f + J * Step;
		for (int32 BX = 0; BX < BlocksX; ++BX)
		{
			const float X0 = MinX + RoadWidth + BX * Step;
			for (const float T : { 0.3f, 0.7f })
			{
				Spots.Add({ FVector(X0 + BlockSize * T, RoadY - CurbOffset, 0.f), 0.f });
				Spots.Add({ FVector(X0 + BlockSize * T, RoadY + CurbOffset, 0.f), 180.f });
			}
		}
	}

	// Leave the family's corner clear (spawn points and job markers), then pick spots at random (same seed = same street).
	Spots.RemoveAll([&](const FSpot& Spot) { return FVector::Dist2D(Spot.Location, HomeCenter) < 4500.f; });
	FRandomStream Stream(Seed + 7);
	for (int32 Index = Spots.Num() - 1; Index > 0; --Index)
	{
		Spots.Swap(Index, Stream.RandRange(0, Index));
	}

	// A few cars in sight of the family's corner (clear of the job markers on the avenues' center lines).
	const TArray<FSpot> HomeSpots = {
		{ HomeCenter + FVector(-1900.f, -CurbOffset, 0.f), 0.f },
		{ HomeCenter + FVector(1900.f, CurbOffset, 0.f), 180.f },
		{ HomeCenter + FVector(-CurbOffset, 1900.f, 0.f), -90.f },
	};
	Spots.Insert(HomeSpots, 0);

	// A Tel Aviv street: mostly white, silver, gray and black, a few colors.
	const FLinearColor Paints[] = {
		FLinearColor(0.85f, 0.85f, 0.85f), FLinearColor(0.85f, 0.85f, 0.85f), FLinearColor(0.45f, 0.46f, 0.48f),
		FLinearColor(0.2f, 0.21f, 0.22f), FLinearColor(0.02f, 0.02f, 0.025f), FLinearColor(0.03f, 0.07f, 0.25f),
		FLinearColor(0.45f, 0.02f, 0.02f), FLinearColor(0.6f, 0.55f, 0.42f), FLinearColor(1.f, 0.1f, 0.f) };

	int32 Spawned = 0;
	const int32 Wanted = NumParkedCars + HomeSpots.Num();
	for (int32 Index = 0; Index < Spots.Num() && Spawned < Wanted; ++Index)
	{
		const FTransform CarTransform(FRotator(0.f, GetActorRotation().Yaw + Spots[Index].Yaw, 0.f), ToWorld(Spots[Index].Location + FVector(0.f, 0.f, 40.f)));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AMishpachaVehicle* Car = World->SpawnActor<AMishpachaVehicle>(ParkedCarClass, CarTransform, Params))
		{
			Car->SetPaintColor(Paints[Stream.RandRange(0, UE_ARRAY_COUNT(Paints) - 1)]);
			++Spawned;
		}
	}
	UE_LOG(LogMishpacha, Log, TEXT("CityGreyboxGenerator: parked %d cars."), Spawned);
}

void ACityGreyboxGenerator::CreateDefaultMissions(const TMap<FName, FVector>& Targets)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const bool bHasVehicles = static_cast<bool>(TActorIterator<AMishpachaVehicle>(World));

	auto MakeMission = [this](FName Id, const FText& Title, const FText& Description, FName Sibling, int32 Reward)
	{
		UMissionDefinition* Mission = NewObject<UMissionDefinition>(this, Id);
		Mission->MissionId = Id;
		Mission->Title = Title;
		Mission->Description = Description;
		Mission->RequiredSibling = Sibling;
		Mission->RewardMoney = Reward;
		DefaultMissions.Add(Mission);
		return Mission;
	};
	// Step builders. Each returns the objective so a dialogue line can be attached with Say().
	auto Add = [](UMissionDefinition* Mission, UMissionObjective* Objective, const FText& Text) -> UMissionObjective*
	{
		Objective->Description = Text;
		Mission->Objectives.Add(Objective);
		return Objective;
	};
	auto Reach = [&Add](UMissionDefinition* Mission, FName Tag, const FText& Text)
	{
		UObjective_ReachLocation* Objective = NewObject<UObjective_ReachLocation>(Mission);
		Objective->TargetActorTag = Tag;
		Objective->Radius = 700.f;
		return Add(Mission, Objective, Text);
	};
	auto EnterCar = [&Add, bHasVehicles](UMissionDefinition* Mission, const FText& Text) -> UMissionObjective*
	{
		if (!bHasVehicles)
		{
			return nullptr; // no cars in the level yet (needs the Vehicle content pack) - skip this step
		}
		return Add(Mission, NewObject<UObjective_EnterVehicle>(Mission), Text);
	};
	auto LoseHeat = [&Add](UMissionDefinition* Mission, const FText& Text)
	{
		return Add(Mission, NewObject<UObjective_LoseHeat>(Mission), Text);
	};
	auto RivalCrew = [&Add](UMissionDefinition* Mission, FName Tag, int32 Count, const FText& Text)
	{
		UObjective_RivalCrew* Objective = NewObject<UObjective_RivalCrew>(Mission);
		Objective->LocationTag = Tag;
		Objective->Count = Count;
		return Add(Mission, Objective, Text);
	};
	auto WaitAt = [&Add](UMissionDefinition* Mission, FName Tag, float Seconds, const FText& Text)
	{
		UObjective_WaitAt* Objective = NewObject<UObjective_WaitAt>(Mission);
		Objective->LocationTag = Tag;
		Objective->Seconds = Seconds;
		return Add(Mission, Objective, Text);
	};
	auto SwitchTo = [&Add](UMissionDefinition* Mission, FName Sibling, const FText& Text)
	{
		UObjective_SwitchToSibling* Objective = NewObject<UObjective_SwitchToSibling>(Mission);
		Objective->SiblingId = Sibling;
		return Add(Mission, Objective, Text);
	};
	auto Say = [](UMissionObjective* Objective, FName Speaker, const FText& Line)
	{
		if (Objective)
		{
			Objective->Speaker = FText::FromName(Speaker);
			Objective->Dialogue = Line;
		}
	};

	// ----- Rafi -----
	UMissionDefinition* RafiJob = MakeMission(FName(TEXT("Rafi_01")),
		LOCTEXT("Rafi01Title", "Collection at the Market"),
		LOCTEXT("Rafi01Desc", "A stall owner in the Old Yafeh market stopped paying. Remind him who runs this city."),
		GreyboxNames::Rafi, 1500);
	Say(Reach(RafiJob, GreyboxNames::Market, LOCTEXT("Rafi01A", "Go to the market (yellow pillar near the beach)")),
		GreyboxNames::Rafi, LOCTEXT("Rafi01ASay", "Three months Itzik hasn't paid. Today he pays for all three."));
	LoseHeat(RafiJob, LOCTEXT("Rafi01B", "If the cops are on you, lose them"));

	UMissionDefinition* RafiJob2 = MakeMission(FName(TEXT("Rafi_02")),
		LOCTEXT("Rafi02Title", "Rival Crew"),
		LOCTEXT("Rafi02Desc", "Some crew from out of town set up shop at Moshe's garage. On our street."),
		GreyboxNames::Rafi, 3500);
	RafiJob2->Prerequisites.Add(RafiJob->MissionId);
	Say(Reach(RafiJob2, GreyboxNames::Garage, LOCTEXT("Rafi02A", "Drive to Moshe's garage")),
		GreyboxNames::Rafi, LOCTEXT("Rafi02ASay", "Moshe called. Strangers in his garage, and they're not there for an oil change."));
	Say(RivalCrew(RafiJob2, GreyboxNames::Garage, 4, LOCTEXT("Rafi02B", "Take out the rival crew (press Q to call the boys)")),
		GreyboxNames::Rafi, LOCTEXT("Rafi02BSay", "Nobody does business in Tel Avir without asking the Sela family."));
	LoseHeat(RafiJob2, LOCTEXT("Rafi02C", "Lose the police"));

	// ----- Shira -----
	UMissionDefinition* ShiraJob = MakeMission(FName(TEXT("Shira_01")),
		LOCTEXT("Shira01Title", "Dad's Phone"),
		LOCTEXT("Shira01Desc", "Dad's phone is sitting in the police evidence warehouse. Get it out before they unlock it."),
		GreyboxNames::Shira, 2500);
	Say(Reach(ShiraJob, GreyboxNames::Warehouse, LOCTEXT("Shira01A", "Get to the evidence warehouse")),
		GreyboxNames::Shira, LOCTEXT("Shira01ASay", "If they read what's on that phone, all three of us go down. Me first."));
	EnterCar(ShiraJob, LOCTEXT("Shira01B", "Grab a car"));
	Reach(ShiraJob, GreyboxNames::Dropoff, LOCTEXT("Shira01C", "Bring the phone to the family drop-off"));

	UMissionDefinition* ShiraJob2 = MakeMission(FName(TEXT("Shira_02")),
		LOCTEXT("Shira02Title", "The Witness"),
		LOCTEXT("Shira02Desc", "A witness against the family is at the hospital. Shira wants to talk to him before the prosecutor does."),
		GreyboxNames::Shira, 3000);
	ShiraJob2->Prerequisites.Add(ShiraJob->MissionId);
	Say(Reach(ShiraJob2, GreyboxNames::Hospital, LOCTEXT("Shira02A", "Go to the hospital")),
		GreyboxNames::Shira, LOCTEXT("Shira02ASay", "I'm his lawyer as of this morning. He just doesn't know it yet."));
	Say(WaitAt(ShiraJob2, GreyboxNames::Hospital, 8.f, LOCTEXT("Shira02B", "Talk to the witness (stay close)")),
		GreyboxNames::Shira, LOCTEXT("Shira02BSay", "Nobody's asking you to lie. Just to remember a little less."));
	EnterCar(ShiraJob2, LOCTEXT("Shira02C", "Get a car"));
	Say(Reach(ShiraJob2, GreyboxNames::Dropoff, LOCTEXT("Shira02D", "Take him somewhere quiet (the family drop-off)")),
		GreyboxNames::Shira, LOCTEXT("Shira02DSay", "Seatbelt. You're no use to anyone dead."));

	// ----- Omer -----
	UMissionDefinition* OmerJob = MakeMission(FName(TEXT("Omer_01")),
		LOCTEXT("Omer01Title", "Last Delivery"),
		LOCTEXT("Omer01Desc", "One last package for the family before the big trip. What could go wrong?"),
		GreyboxNames::Omer, 2000);
	Say(EnterCar(OmerJob, LOCTEXT("Omer01A", "Get a ride")),
		GreyboxNames::Omer, LOCTEXT("Omer01ASay", "Rafi says it's just shoes. Nobody wraps shoes like this."));
	Reach(OmerJob, GreyboxNames::Garage, LOCTEXT("Omer01B", "Deliver the package to Moshe's garage"));
	LoseHeat(OmerJob, LOCTEXT("Omer01C", "Lose anyone following you"));

	UMissionDefinition* OmerJob2 = MakeMission(FName(TEXT("Omer_02")),
		LOCTEXT("Omer02Title", "Eye in the Sky"),
		LOCTEXT("Omer02Desc", "The warehouse has new guards, and they aren't police. Omer gets a look from above first."),
		GreyboxNames::Omer, 3000);
	OmerJob2->Prerequisites.Add(OmerJob->MissionId);
	Say(Reach(OmerJob2, GreyboxNames::Warehouse, LOCTEXT("Omer02A", "Go to the evidence warehouse")),
		GreyboxNames::Omer, LOCTEXT("Omer02ASay", "Two years in the drone unit. Finally useful for something."));
	Say(RivalCrew(OmerJob2, GreyboxNames::Warehouse, 3, LOCTEXT("Omer02B", "Take out the guards (press Q: the drone marks them)")),
		GreyboxNames::Omer, LOCTEXT("Omer02BSay", "Three on the ground. They haven't seen me. Yet."));
	LoseHeat(OmerJob2, LOCTEXT("Omer02C", "Lose the police"));

	// ----- Two siblings: Rafi starts, Shira finishes -----
	UMissionDefinition* FamilyJob = MakeMission(FName(TEXT("Family_01")),
		LOCTEXT("Family01Title", "The Handover"),
		LOCTEXT("Family01Desc", "Rafi has the money. Shira has the judge. They need to meet without anyone seeing them together."),
		GreyboxNames::Rafi, 5000);
	FamilyJob->bAllowSwitching = true;
	FamilyJob->Prerequisites.Add(RafiJob->MissionId);
	FamilyJob->Prerequisites.Add(ShiraJob->MissionId);
	Say(Reach(FamilyJob, GreyboxNames::Market, LOCTEXT("Family01A", "Rafi: leave the envelope at the market")),
		GreyboxNames::Rafi, LOCTEXT("Family01ASay", "Shira, it's at the market. Under the pomegranates. Don't make that face."));
	Say(SwitchTo(FamilyJob, GreyboxNames::Shira, LOCTEXT("Family01B", "Switch to Shira (press 2)")),
		GreyboxNames::Shira, LOCTEXT("Family01BSay", "This is the last time, Rafi. I mean it this time."));
	Reach(FamilyJob, GreyboxNames::Market, LOCTEXT("Family01C", "Shira: pick up the envelope at the market"));
	Say(Reach(FamilyJob, GreyboxNames::Hospital, LOCTEXT("Family01D", "Deliver it to the judge's driver at the hospital")),
		GreyboxNames::Shira, LOCTEXT("Family01DSay", "Hospital parking. Nobody remembers faces at a hospital."));

	// Markers on the avenues around the central intersection. Each sibling's next job takes the
	// same spot once the previous one is done (see AMissionStartMarker::RefreshAvailability).
	const FVector Center = IntersectionLocal(BlocksX / 2, BlocksY / 2);
	const float Off = (BlockSize + RoadWidth) * 0.4f;
	const TPair<UMissionDefinition*, FVector> Placements[] = {
		TPair<UMissionDefinition*, FVector>(RafiJob, Center + FVector(0.f, -Off, 0.f)),
		TPair<UMissionDefinition*, FVector>(RafiJob2, Center + FVector(0.f, -Off, 0.f)),
		TPair<UMissionDefinition*, FVector>(ShiraJob, Center + FVector(Off, 0.f, 0.f)),
		TPair<UMissionDefinition*, FVector>(ShiraJob2, Center + FVector(Off, 0.f, 0.f)),
		TPair<UMissionDefinition*, FVector>(OmerJob, Center + FVector(0.f, Off, 0.f)),
		TPair<UMissionDefinition*, FVector>(OmerJob2, Center + FVector(0.f, Off, 0.f)),
		TPair<UMissionDefinition*, FVector>(FamilyJob, Center + FVector(-Off, 0.f, 0.f)),
	};
	for (const TPair<UMissionDefinition*, FVector>& Placement : Placements)
	{
		const FTransform MarkerTransform(ToWorld(Placement.Value + FVector(0.f, 0.f, 100.f)));
		if (AMissionStartMarker* Marker = World->SpawnActorDeferred<AMissionStartMarker>(AMissionStartMarker::StaticClass(), MarkerTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Marker->SetMission(Placement.Key);
			Marker->FinishSpawning(MarkerTransform);
		}
	}
}

// ---------------------------------------------------------------------------
// Pedestrians (server)
// ---------------------------------------------------------------------------

void ACityGreyboxGenerator::MaintainTraffic()
{
	UWorld* World = GetWorld();
	TArray<FVector> PlayerLocations;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			PlayerLocations.Add(Pawn->GetActorLocation());
		}
	}
	if (PlayerLocations.Num() == 0)
	{
		return;
	}
	auto DistToPlayers = [&PlayerLocations](const FVector& Location)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FVector& Player : PlayerLocations)
		{
			Best = FMath::Min(Best, FVector::Dist2D(Player, Location));
		}
		return Best;
	};

	// Drop cars that drove off far away (or were stolen: then they're ordinary cars now).
	for (int32 Index = TrafficCars.Num() - 1; Index >= 0; --Index)
	{
		APawn* Car = TrafficCars[Index].Get();
		const bool bStillTraffic = Car && Car->GetController() && !Car->GetController()->IsPlayerController();
		if (!bStillTraffic)
		{
			TrafficCars.RemoveAtSwap(Index);
			continue;
		}
		if (DistToPlayers(Car->GetActorLocation()) > 13000.0)
		{
			if (AController* Driver = Car->GetController())
			{
				Driver->Destroy();
			}
			Car->Destroy();
			TrafficCars.RemoveAtSwap(Index);
		}
	}

	// Now and then: how is traffic doing (moving vs. standing)?
	if (++TrafficReportCounter % 8 == 0)
	{
		int32 Moving = 0;
		for (const TWeakObjectPtr<APawn>& Car : TrafficCars)
		{
			Moving += (Car.IsValid() && Car->GetVelocity().Size() > 200.f) ? 1 : 0;
		}
		UE_LOG(LogMishpacha, Log, TEXT("Traffic: %d cars, %d moving."), TrafficCars.Num(), Moving);
	}

	// One new car per tick: on a block-long stretch 30-90 m from a player, in the right-hand lane.
	if (TrafficCars.Num() >= MaxTraffic)
	{
		return;
	}
	for (int32 Attempt = 0; Attempt < 10; ++Attempt)
	{
		const FIntPoint From(FMath::RandRange(0, BlocksX), FMath::RandRange(0, BlocksY));
		const FIntPoint Steps[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		const FIntPoint To = From + Steps[FMath::RandRange(0, 3)];
		if (To.X < 0 || To.Y < 0 || To.X > BlocksX || To.Y > BlocksY)
		{
			continue;
		}
		const FVector A = GetIntersection(From.X, From.Y);
		const FVector B = GetIntersection(To.X, To.Y);
		const FVector Direction = (B - A).GetSafeNormal2D();
		const FVector Spot = FMath::Lerp(A, B, FMath::FRandRange(0.3f, 0.7f)) + FVector(-Direction.Y, Direction.X, 0.f) * 280.f;
		const double Distance = DistToPlayers(Spot);
		if (Distance < 3000.0 || Distance > 9000.0)
		{
			continue;
		}
		// Don't drop it onto something.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TrafficSpawn), false);
		if (World->OverlapAnyTestByChannel(Spot + FVector(0.f, 0.f, 120.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(260.f, 140.f, 80.f)), Params))
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		AMishpachaTrafficCar* Car = World->SpawnActor<AMishpachaTrafficCar>(AMishpachaTrafficCar::StaticClass(), Spot + FVector(0.f, 0.f, 50.f), Direction.Rotation(), SpawnParams);
		if (!Car)
		{
			continue;
		}
		const FLinearColor Paints[] = { FLinearColor(0.85f, 0.85f, 0.85f), FLinearColor(0.45f, 0.46f, 0.48f), FLinearColor(0.02f, 0.02f, 0.025f),
			FLinearColor(0.03f, 0.07f, 0.25f), FLinearColor(0.45f, 0.02f, 0.02f), FLinearColor(0.8f, 0.6f, 0.05f) }; // ... and the odd yellow taxi
		Car->SetPaintColor(Paints[FMath::RandRange(0, UE_ARRAY_COUNT(Paints) - 1)]);
		if (AMishpachaTrafficAI* Driver = Cast<AMishpachaTrafficAI>(Car->GetController()))
		{
			Driver->SetLeg(From, To);
		}
		TrafficCars.Add(Car);
		return;
	}
}

void ACityGreyboxGenerator::MaintainCivilians()
{
	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState || !CivilianClass || SidewalkPoints.Num() == 0)
	{
		return;
	}

	TArray<FVector> PlayerLocations;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (const APawn* PlayerPawn = PlayerState ? PlayerState->GetPawn() : nullptr)
		{
			PlayerLocations.Add(PlayerPawn->GetActorLocation());
		}
	}
	if (PlayerLocations.Num() == 0)
	{
		return;
	}

	auto NearestPlayerDistSq = [&PlayerLocations](const FVector& Location)
	{
		double Best = TNumericLimits<double>::Max();
		for (const FVector& PlayerLocation : PlayerLocations)
		{
			Best = FMath::Min(Best, FVector::DistSquared2D(Location, PlayerLocation));
		}
		return Best;
	};

	// Despawn the far away and the long dead.
	for (int32 Index = Civilians.Num() - 1; Index >= 0; --Index)
	{
		APawn* Civilian = Civilians[Index].Get();
		if (!Civilian)
		{
			Civilians.RemoveAtSwap(Index);
			continue;
		}
		if (NearestPlayerDistSq(Civilian->GetActorLocation()) > FMath::Square(CivilianDespawnRadius))
		{
			if (AController* CivilianController = Civilian->GetController())
			{
				CivilianController->Destroy();
			}
			Civilian->Destroy();
			Civilians.RemoveAtSwap(Index);
		}
	}

	// Top up, a few per tick, on sidewalks near (but not right next to) players.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;

	int32 SpawnBudget = 4;
	for (int32 Attempt = 0; Attempt < 20 && SpawnBudget > 0 && Civilians.Num() < MaxCivilians; ++Attempt)
	{
		const FVector Candidate = ToWorld(SidewalkPoints[FMath::RandRange(0, SidewalkPoints.Num() - 1)] + FVector(0.f, 0.f, 100.f));
		const double DistSq = NearestPlayerDistSq(Candidate);
		if (DistSq > FMath::Square(CivilianSpawnRadius) || DistSq < FMath::Square(1500.0))
		{
			continue;
		}

		// Tel Aviv has as many street cats as people (almost).
		UClass* SpawnClass = FMath::FRand() < CatShare ? AMishpachaCat::StaticClass() : CivilianClass.Get();
		APawn* Civilian = World->SpawnActor<APawn>(SpawnClass, SpawnClass == AMishpachaCat::StaticClass() ? Candidate - FVector(0.f, 0.f, 75.f) : Candidate,
			FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
		if (Civilian)
		{
			if (!Civilian->GetController())
			{
				Civilian->SpawnDefaultController();
			}
			Civilians.Add(Civilian);
			--SpawnBudget;
		}
	}
}

#undef LOCTEXT_NAMESPACE
