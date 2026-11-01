// MISHPACHA - procedural gray-box "Tel Avir".
#include "World/CityGreyboxGenerator.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Family/SiblingSpawnPoint.h"
#include "Missions/MissionDefinition.h"
#include "Missions/MissionObjectives.h"
#include "Missions/MissionStartMarker.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Mishpacha.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/TargetPoint.h"
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
	ISM->SetMobility(EComponentMobility::Static);
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

	CivilianClass = AMishpachaCivilian::StaticClass();
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

	SetupISM(RoadsISM, Cube, BaseMaterial, FLinearColor(0.04f, 0.04f, 0.045f), true);
	SetupISM(SidewalksISM, Cube, BaseMaterial, FLinearColor(0.35f, 0.34f, 0.32f), true);
	SetupISM(BuildingsWhiteISM, Cube, BaseMaterial, FLinearColor(0.85f, 0.85f, 0.82f), true);
	SetupISM(BuildingsCreamISM, Cube, BaseMaterial, FLinearColor(0.8f, 0.72f, 0.58f), true);
	SetupISM(TowersISM, Cube, BaseMaterial, FLinearColor(0.25f, 0.33f, 0.4f), true);
	SetupISM(RoofDetailsISM, Cube, BaseMaterial, FLinearColor(0.15f, 0.15f, 0.17f), false);
	SetupISM(LaneMarksISM, Cube, BaseMaterial, FLinearColor(0.9f, 0.9f, 0.85f), false);
	SetupISM(SandISM, Cube, BaseMaterial, FLinearColor(0.76f, 0.66f, 0.45f), true);
	SetupISM(SeaISM, Cube, BaseMaterial, FLinearColor(0.02f, 0.18f, 0.35f), true);
	SetupISM(BeaconsISM, Cylinder ? Cylinder : Cube, BaseMaterial, FLinearColor(1.f, 0.8f, 0.1f), false);

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

	// --- Asphalt base covering the whole grid (top at Z = 0) ---
	TArray<FTransform> Roads;
	Roads.Add(Box(FVector(0.f, 0.f, -50.f), FVector(ExtentX, ExtentY, 100.f)));
	RoadsISM->AddInstances(Roads, false);

	// --- Blocks: sidewalk slab + 2x2 lots of buildings ---
	TArray<FTransform> Sidewalks, White, Cream, Towers, Roof;
	SidewalkPoints.Reset();

	for (int32 BX = 0; BX < BlocksX; ++BX)
	{
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			const float X0 = MinX + RoadWidth + BX * Step;
			const float Y0 = MinY + RoadWidth + BY * Step;
			const FVector BlockCenter(X0 + BlockSize * 0.5f, Y0 + BlockSize * 0.5f, 0.f);

			Sidewalks.Add(Box(BlockCenter + FVector(0.f, 0.f, SidewalkHeight * 0.5f), FVector(BlockSize, BlockSize, SidewalkHeight)));

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
						continue;
					}

					float Height;
					if (bTowerDistrict)
					{
						Height = Stream.FRandRange(6000.f, 15000.f);
						Towers.Add(Box(LotCenter + FVector(0.f, 0.f, SidewalkHeight + Height * 0.5f), FVector(SizeX, SizeY, Height)));
						continue;
					}

					// Bauhaus low-rises: 2-5 floors on "pilotis", the odd 8-12 floor block.
					const int32 Floors = (Stream.FRand() < 0.1f) ? Stream.RandRange(8, 12) : Stream.RandRange(2, 5);
					Height = Floors * 320.f + 100.f;
					const FTransform Building = Box(LotCenter + FVector(0.f, 0.f, SidewalkHeight + Height * 0.5f), FVector(SizeX, SizeY, Height));
					if (Stream.FRand() < 0.6f)
					{
						White.Add(Building);
					}
					else
					{
						Cream.Add(Building);
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
				}
			}
		}
	}

	SidewalksISM->AddInstances(Sidewalks, false);
	BuildingsWhiteISM->AddInstances(White, false);
	BuildingsCreamISM->AddInstances(Cream, false);
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

	// --- Beach and sea on the west (negative X), like the real coast ---
	TArray<FTransform> Sand, Sea;
	Sand.Add(Box(FVector(MinX - BeachWidth * 0.5f, 0.f, -60.f), FVector(BeachWidth, ExtentY + 4000.f, 100.f)));
	const float SeaWidth = 40000.f;
	Sea.Add(Box(FVector(MinX - BeachWidth - SeaWidth * 0.5f, 0.f, -150.f), FVector(SeaWidth, ExtentY + 40000.f, 100.f)));
	SandISM->AddInstances(Sand, false);
	SeaISM->AddInstances(Sea, false);

	// --- Destination beacons (tall yellow pillars over mission targets) ---
	TArray<FTransform> Beacons;
	const FIntPoint BeaconCells[] = { FIntPoint(1, 1), FIntPoint(BlocksX - 1, 2), FIntPoint(BlocksX - 2, BlocksY - 1), FIntPoint(1, BlocksY - 2), FIntPoint(1, BlocksY - 1) };
	for (const FIntPoint& Cell : BeaconCells)
	{
		const FVector Spot = IntersectionLocal(Cell.X, Cell.Y);
		Beacons.Add(FTransform(FRotator::ZeroRotator, Spot + FVector(0.f, 0.f, 1500.f), FVector(0.5f, 0.5f, 30.f)));
	}
	BeaconsISM->AddInstances(Beacons, false);

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
			SkyComponent->bRealTimeCapture = true;
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

	// --- Default missions, unless the level already has its own markers ---
	const bool bHasMarkers = static_cast<bool>(TActorIterator<AMissionStartMarker>(World));
	if (!bHasMarkers)
	{
		CreateDefaultMissions(Targets);
	}
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
	auto Reach = [](UMissionDefinition* Mission, FName Tag, const FText& Text)
	{
		UObjective_ReachLocation* Objective = NewObject<UObjective_ReachLocation>(Mission);
		Objective->TargetActorTag = Tag;
		Objective->Radius = 700.f;
		Objective->Description = Text;
		Mission->Objectives.Add(Objective);
	};
	auto EnterCar = [bHasVehicles](UMissionDefinition* Mission, const FText& Text)
	{
		if (!bHasVehicles)
		{
			return; // no cars in the level yet (needs the Vehicle content pack) - skip this step
		}
		UObjective_EnterVehicle* Objective = NewObject<UObjective_EnterVehicle>(Mission);
		Objective->Description = Text;
		Mission->Objectives.Add(Objective);
	};
	auto LoseHeat = [](UMissionDefinition* Mission, const FText& Text)
	{
		UObjective_LoseHeat* Objective = NewObject<UObjective_LoseHeat>(Mission);
		Objective->Description = Text;
		Mission->Objectives.Add(Objective);
	};

	UMissionDefinition* RafiJob = MakeMission(FName(TEXT("Rafi_01")),
		LOCTEXT("Rafi01Title", "Collection at the Market"),
		LOCTEXT("Rafi01Desc", "A stall owner in the Old Yafeh market stopped paying. Remind him who runs this city."),
		GreyboxNames::Rafi, 1500);
	Reach(RafiJob, GreyboxNames::Market, LOCTEXT("Rafi01A", "Go to the market (yellow pillar near the beach)"));
	LoseHeat(RafiJob, LOCTEXT("Rafi01B", "If the cops are on you, lose them"));

	UMissionDefinition* ShiraJob = MakeMission(FName(TEXT("Shira_01")),
		LOCTEXT("Shira01Title", "Dad's Phone"),
		LOCTEXT("Shira01Desc", "Dad's phone is sitting in the police evidence warehouse. Get it out before they unlock it."),
		GreyboxNames::Shira, 2500);
	Reach(ShiraJob, GreyboxNames::Warehouse, LOCTEXT("Shira01A", "Get to the evidence warehouse"));
	EnterCar(ShiraJob, LOCTEXT("Shira01B", "Grab a car"));
	Reach(ShiraJob, GreyboxNames::Dropoff, LOCTEXT("Shira01C", "Bring the phone to the family drop-off"));

	UMissionDefinition* OmerJob = MakeMission(FName(TEXT("Omer_01")),
		LOCTEXT("Omer01Title", "Last Delivery"),
		LOCTEXT("Omer01Desc", "One last package for the family before the big trip. What could go wrong?"),
		GreyboxNames::Omer, 2000);
	EnterCar(OmerJob, LOCTEXT("Omer01A", "Get a ride"));
	Reach(OmerJob, GreyboxNames::Garage, LOCTEXT("Omer01B", "Deliver the package to Moshe's garage"));
	LoseHeat(OmerJob, LOCTEXT("Omer01C", "Lose anyone following you"));

	// Markers on the avenues around the central intersection.
	const FVector Center = IntersectionLocal(BlocksX / 2, BlocksY / 2);
	const float Off = (BlockSize + RoadWidth) * 0.4f;
	const TPair<UMissionDefinition*, FVector> Placements[] = {
		TPair<UMissionDefinition*, FVector>(RafiJob, Center + FVector(0.f, -Off, 0.f)),
		TPair<UMissionDefinition*, FVector>(ShiraJob, Center + FVector(Off, 0.f, 0.f)),
		TPair<UMissionDefinition*, FVector>(OmerJob, Center + FVector(0.f, Off, 0.f)),
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

		APawn* Civilian = World->SpawnActor<APawn>(CivilianClass, Candidate, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
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
