// MISHPACHA - spawns and directs police based on players' Heat.
#include "Heat/PoliceDispatchSubsystem.h"
#include "Heat/PoliceDispatchConfig.h"
#include "Heat/HeatComponent.h"
#include "Heat/CrimeLibrary.h"
#include "Core/MishpachaGameModeBase.h"
#include "AIController.h"
#include "AI/MishpachaPoliceAIController.h"
#include "Vehicles/MishpachaPoliceCar.h"
#include "World/CityGreyboxGenerator.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

bool UPoliceDispatchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPoliceDispatchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() == NM_Client)
	{
		return;
	}
	InWorld.GetTimerManager().SetTimer(RefreshTimer, this, &UPoliceDispatchSubsystem::Refresh, 1.f, true);
}

void UPoliceDispatchSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Units.Reset();
	Super::Deinitialize();
}

const UPoliceDispatchConfig* UPoliceDispatchSubsystem::GetConfig() const
{
	const UWorld* World = GetWorld();
	const AMishpachaGameModeBase* GameMode = World ? World->GetAuthGameMode<AMishpachaGameModeBase>() : nullptr;
	return GameMode ? GameMode->GetPoliceConfig() : nullptr;
}

void UPoliceDispatchSubsystem::Refresh()
{
	UWorld* World = GetWorld();
	const UPoliceDispatchConfig* Config = GetConfig();
	AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!Config || !GameState)
	{
		return;
	}

	// 1) Remove dead units and units that are no longer needed.
	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		const FPoliceUnitRecord& Unit = Units[Index];
		APawn* UnitPawn = Unit.Pawn.Get();
		APawn* TargetPawn = Unit.Target.Get();
		if (!UnitPawn)
		{
			Units.RemoveAtSwap(Index);
			continue;
		}

		const UHeatComponent* TargetHeat = UCrimeLibrary::FindHeatComponent(TargetPawn);
		const bool bTargetWanted = TargetHeat && TargetHeat->GetHeatLevel() > 0;
		const double Distance = TargetPawn ? FVector::Dist(UnitPawn->GetActorLocation(), TargetPawn->GetActorLocation()) : TNumericLimits<double>::Max();
		if (Distance > Config->DespawnDistance || (!bTargetWanted && Distance > Config->MinSpawnDistance * 0.5f))
		{
			DespawnUnit(Unit);
			Units.RemoveAtSwap(Index);
		}
	}

	// 2) For each wanted player: sight checks, chase orders, reinforcements.
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		APawn* Target = PlayerState ? PlayerState->GetPawn() : nullptr;
		UHeatComponent* Heat = PlayerState ? PlayerState->FindComponentByClass<UHeatComponent>() : nullptr;
		if (!Target || !Heat || Heat->GetHeatLevel() <= 0)
		{
			continue;
		}

		int32 AssignedCount = 0;
		for (const FPoliceUnitRecord& Unit : Units)
		{
			if (Unit.Target.Get() != Target)
			{
				continue;
			}
			++AssignedCount;

			APawn* UnitPawn = Unit.Pawn.Get();
			if (!UnitPawn)
			{
				continue;
			}

			if (FVector::Dist(UnitPawn->GetActorLocation(), Target->GetActorLocation()) <= Config->SightDistance && HasLineOfSight(UnitPawn, Target))
			{
				Heat->NotifySpottedByPolice(Target->GetActorLocation());
			}

			if (AMishpachaPoliceAIController* PoliceAI = Cast<AMishpachaPoliceAIController>(UnitPawn->GetController()))
			{
				// Built-in officers run their own chase/shoot logic; just tell them who to hunt.
				PoliceAI->SetTarget(Target);
			}
			else if (AAIController* AI = Cast<AAIController>(UnitPawn->GetController()))
			{
				AI->MoveToLocation(Heat->GetLastKnownLocation(), 300.f);
			}
		}

		if (Heat->GetHeatLevel() >= RoadblockFromHeatLevel && Units.Num() + 4 <= Config->MaxTotalUnits)
		{
			TryRoadblock(Target);
		}

		const FPoliceHeatResponse* Response = Config->GetResponse(Heat->GetHeatLevel());
		if (Response && Response->UnitClasses.Num() > 0 && AssignedCount < Response->MaxUnits && Units.Num() < Config->MaxTotalUnits)
		{
			// One spawn per refresh per player -> reinforcements arrive staggered.
			const TSubclassOf<APawn> UnitClass = Response->UnitClasses[FMath::RandRange(0, Response->UnitClasses.Num() - 1)];
			TrySpawnUnit(*Config, UnitClass, Target);
		}
	}
}

bool UPoliceDispatchSubsystem::TrySpawnUnit(const UPoliceDispatchConfig& Config, TSubclassOf<APawn> UnitClass, APawn* Target)
{
	UWorld* World = GetWorld();
	FVector SpawnLocation;
	if (!World || !UnitClass || !Target || !FindSpawnLocation(Config, Target->GetActorLocation(), SpawnLocation))
	{
		return false;
	}

	float Yaw = (Target->GetActorLocation() - SpawnLocation).Rotation().Yaw;
	FVector SpawnOffset(0.f, 0.f, 120.f);
	if (UnitClass->IsChildOf(AMishpachaVehicle::StaticClass()))
	{
		// Patrol cars start on a road, lined up with it, heading the suspect's way.
		TActorIterator<ACityGreyboxGenerator> Grid(World);
		if (Grid)
		{
			SpawnLocation = Grid->SnapToRoad(SpawnLocation);
			const FVector ToTarget = Target->GetActorLocation() - SpawnLocation;
			Yaw = FMath::Abs(ToTarget.X) > FMath::Abs(ToTarget.Y) ? (ToTarget.X > 0.f ? 0.f : 180.f) : (ToTarget.Y > 0.f ? 90.f : -90.f);
		}
		SpawnOffset = FVector(0.f, 0.f, 60.f);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* UnitPawn = World->SpawnActor<APawn>(UnitClass, SpawnLocation + SpawnOffset, FRotator(0.f, Yaw, 0.f), Params);
	if (!UnitPawn)
	{
		return false;
	}

	UnitPawn->Tags.AddUnique(FName(TEXT("Police")));
	if (!UnitPawn->GetController())
	{
		UnitPawn->SpawnDefaultController();
	}

	FPoliceUnitRecord Record;
	Record.Pawn = UnitPawn;
	Record.Target = Target;
	Units.Add(Record);
	return true;
}

bool UPoliceDispatchSubsystem::FindSpawnLocation(const UPoliceDispatchConfig& Config, const FVector& Origin, FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		const float Angle = FMath::FRandRange(0.f, UE_TWO_PI);
		const float RingDistance = FMath::FRandRange(Config.MinSpawnDistance, Config.MaxSpawnDistance);
		const FVector Candidate = Origin + FVector(FMath::Cos(Angle) * RingDistance, FMath::Sin(Angle) * RingDistance, 0.f);

		// Prefer the navmesh when the level has one...
		FNavLocation NavLocation;
		if (NavSys && NavSys->ProjectPointToNavigation(Candidate, NavLocation, FVector(2000.f, 2000.f, 5000.f)))
		{
			OutLocation = NavLocation.Location;
			return true;
		}

		// ...otherwise (gray-box levels without a NavMeshBoundsVolume) find open ground with a trace.
		FHitResult Hit;
		const FVector TraceStart = Candidate + FVector(0.f, 0.f, 5000.f);
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, 5000.f);
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility) && Hit.ImpactNormal.Z > 0.9f)
		{
			// Skip rooftops: the spot must be close to the target's height.
			if (FMath::Abs(Hit.ImpactPoint.Z - Origin.Z) < 300.f)
			{
				OutLocation = Hit.ImpactPoint;
				return true;
			}
		}
	}
	return false;
}

bool UPoliceDispatchSubsystem::HasLineOfSight(const AActor* From, const AActor* To) const
{
	UWorld* World = GetWorld();
	if (!World || !From || !To)
	{
		return false;
	}

	const APawn* FromPawn = Cast<APawn>(From);
	const FVector Start = FromPawn ? FromPawn->GetPawnViewLocation() : From->GetActorLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceSight), false, From);
	Params.AddIgnoredActor(To);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Start, To->GetActorLocation(), ECC_Visibility, Params);
}

void UPoliceDispatchSubsystem::DespawnUnit(const FPoliceUnitRecord& Unit)
{
	if (APawn* UnitPawn = Unit.Pawn.Get())
	{
		if (AController* UnitController = UnitPawn->GetController())
		{
			UnitController->Destroy();
		}
		UnitPawn->Destroy();
	}
}

void UPoliceDispatchSubsystem::RegisterUnit(APawn* UnitPawn, APawn* Target)
{
	if (UnitPawn)
	{
		UnitPawn->Tags.AddUnique(FName(TEXT("Police")));
		FPoliceUnitRecord Record;
		Record.Pawn = UnitPawn;
		Record.Target = Target;
		Units.Add(Record);
	}
}

void UPoliceDispatchSubsystem::TryRoadblock(APawn* Target)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	// Only against someone driving, only on the street grid, not too often.
	if (!Cast<AMishpachaVehicle>(Target) || Now - LastRoadblockTime < RoadblockInterval)
	{
		return;
	}
	TActorIterator<ACityGreyboxGenerator> Grid(World);
	const FVector Velocity = Target->GetVelocity();
	if (!Grid || Velocity.Size2D() < 500.f)
	{
		return;
	}

	// The intersection ~60 m ahead of where the suspect is heading.
	const FVector Ahead = Target->GetActorLocation() + Velocity.GetSafeNormal2D() * 6000.f;
	const FVector Junction = Grid->GetNearestIntersection(Ahead);
	const FVector ToJunction = (Junction - Target->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	if (ToJunction.Size() < 3500.f || FVector::DotProduct(ToJunction.GetSafeNormal(), Velocity.GetSafeNormal2D()) < 0.5f)
	{
		return; // too close to be fair, or not actually ahead
	}
	LastRoadblockTime = Now;

	// Two cars broadside across the approach.
	const FVector Approach = ToJunction.GetSafeNormal();
	const FVector Across(-Approach.Y, Approach.X, 0.f);
	for (const float Side : { -1.f, 1.f })
	{
		const FTransform CarTransform(FRotator(0.f, Across.Rotation().Yaw + (Side > 0.f ? 15.f : -15.f), 0.f), Junction + Across * (Side * 290.f) + FVector(0.f, 0.f, 60.f));
		AMishpachaPoliceCar* Car = World->SpawnActorDeferred<AMishpachaPoliceCar>(AMishpachaPoliceCar::StaticClass(), CarTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (Car)
		{
			Car->bRoadblock = true;
			Car->FinishSpawning(CarTransform);
			RegisterUnit(Car, Target);
		}
	}
}

void UPoliceDispatchSubsystem::DespawnAllUnits()
{
	for (const FPoliceUnitRecord& Unit : Units)
	{
		DespawnUnit(Unit);
	}
	Units.Reset();
}
