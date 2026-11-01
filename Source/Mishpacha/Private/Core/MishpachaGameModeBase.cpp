// MISHPACHA - rules shared by Story and Online modes.
#include "Core/MishpachaGameModeBase.h"
#include "Core/MishpachaPlayerController.h"
#include "Core/MishpachaPlayerState.h"
#include "Core/MishpachaGameState.h"
#include "Characters/MishpachaCharacter.h"
#include "Heat/HeatComponent.h"
#include "Missions/MissionSubsystem.h"
#include "UI/MishpachaHUD.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Family/SiblingSpawnPoint.h"
#include "Heat/PoliceDispatchConfig.h"
#include "World/CityGreyboxGenerator.h"
#include "World/MishpachaDayNight.h"
#include "Mishpacha.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaGameMode"

AMishpachaGameModeBase::AMishpachaGameModeBase()
{
	PlayerControllerClass = AMishpachaPlayerController::StaticClass();
	PlayerStateClass = AMishpachaPlayerState::StaticClass();
	GameStateClass = AMishpachaGameState::StaticClass();
	HUDClass = AMishpachaHUD::StaticClass();
	DefaultPawnClass = AMishpachaCharacter::StaticClass();
}

void AMishpachaGameModeBase::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	if (!PoliceConfig && bAutoPolice)
	{
		// Built-in response: more officers per heat level; they shoot from level 3 (see AMishpachaPoliceAIController).
		UPoliceDispatchConfig* DefaultConfig = NewObject<UPoliceDispatchConfig>(this, TEXT("DefaultPoliceConfig"));
		const int32 UnitsPerLevel[] = { 1, 2, 3, 5, 7 };
		for (const int32 Units : UnitsPerLevel)
		{
			FPoliceHeatResponse Response;
			Response.UnitClasses.Add(AMishpachaPolicePawn::StaticClass());
			Response.MaxUnits = Units;
			DefaultConfig->ResponseByLevel.Add(Response);
		}
		DefaultConfig->MinSpawnDistance = 3500.f;
		DefaultConfig->MaxSpawnDistance = 6500.f;
		DefaultConfig->DespawnDistance = 16000.f;
		DefaultConfig->SightDistance = 4500.f;
		DefaultConfig->MaxTotalUnits = 16;
		PoliceConfig = DefaultConfig;
	}
}

void AMishpachaGameModeBase::StartPlay()
{
	EnsureAutoContent();
	Super::StartPlay();
}

void AMishpachaGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	EnsureAutoContent();
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AMishpachaGameModeBase::EnsureAutoContent()
{
	if (bAutoContentChecked)
	{
		return;
	}
	bAutoContentChecked = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bAutoDayNight && !TActorIterator<AMishpachaDayNight>(World))
	{
		FActorSpawnParameters DayNightParams;
		DayNightParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<AMishpachaDayNight>(AMishpachaDayNight::StaticClass(), FTransform::Identity, DayNightParams);
	}
	if (!bAutoGreybox)
	{
		return;
	}

	TActorIterator<ACityGreyboxGenerator> PlacedGenerator(World);
	if (PlacedGenerator)
	{
		PlacedGenerator->Generate(); // placed by hand: make sure it's built before players spawn
		return;
	}
	if (TActorIterator<ASiblingSpawnPoint>(World))
	{
		return; // the level was set up by hand - leave it alone
	}

	// Put the city on whatever floor the level has at the origin (Basic level floor, landscape...).
	FVector Origin = FVector::ZeroVector;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GreyboxGround), false);
	if (World->LineTraceSingleByChannel(Hit, FVector(0.f, 0.f, 50000.f), FVector(0.f, 0.f, -50000.f), ECC_WorldStatic, Params))
	{
		Origin.Z = Hit.ImpactPoint.Z;
	}
	Origin.Z += 5.f; // avoid z-fighting with an existing floor

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ACityGreyboxGenerator* Generator = World->SpawnActor<ACityGreyboxGenerator>(Origin, FRotator::ZeroRotator, SpawnParams))
	{
		Generator->Generate();
		UE_LOG(LogMishpacha, Log, TEXT("Auto setup: spawned the gray-box city (no sibling spawn points in this level)."));
	}
}

void AMishpachaGameModeBase::OnCharacterDied(AMishpachaCharacter* DeadCharacter, AController* VictimController, AController* Killer)
{
	if (UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Missions->NotifyPlayerDied(VictimController);
	}

	APlayerController* PC = Cast<APlayerController>(VictimController);
	if (!PC || !DeadCharacter)
	{
		return;
	}

	int32 Fee = 0;
	if (AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>())
	{
		Fee = FMath::Min(HospitalFee, PS->GetMoney());
		PS->AddMoney(-Fee);
		if (UHeatComponent* Heat = PS->GetHeatComponent())
		{
			Heat->ClearHeat();
		}
	}

	if (AMishpachaPlayerController* MPC = Cast<AMishpachaPlayerController>(PC))
	{
		MPC->ClientShowNotification(FText::Format(LOCTEXT("HospitalBill", "You were rushed to the hospital. Bill: NIS {0}"), FText::AsNumber(Fee)));
	}

	const FVector DeathLocation = DeadCharacter->GetActorLocation();
	TWeakObjectPtr<AController> WeakController = PC;
	FTimerHandle RespawnHandle;
	GetWorldTimerManager().SetTimer(RespawnHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakController, DeathLocation]()
	{
		if (AController* ControllerToRespawn = WeakController.Get())
		{
			RespawnPlayer(ControllerToRespawn, DeathLocation);
		}
	}), FMath::Max(RespawnDelay, 0.1f), false);
}

void AMishpachaGameModeBase::RespawnPlayer(AController* DeadController, const FVector& DeathLocation)
{
	if (!DeadController)
	{
		return;
	}
	RestartPlayerAtTransform(DeadController, FindHospitalTransform(DeathLocation, DeadController));
}

FTransform AMishpachaGameModeBase::FindHospitalTransform(const FVector& NearLocation, AController* ForController)
{
	TArray<AActor*> Hospitals;
	UGameplayStatics::GetAllActorsWithTag(this, HospitalTag, Hospitals);

	AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (AActor* Hospital : Hospitals)
	{
		const double DistSq = FVector::DistSquared(Hospital->GetActorLocation(), NearLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Hospital;
		}
	}

	FTransform Result(NearLocation + FVector(0.f, 0.f, 200.f));
	if (Best)
	{
		Result = Best->GetActorTransform();
	}
	else if (AActor* PlayerStartActor = FindPlayerStart(ForController))
	{
		Result = PlayerStartActor->GetActorTransform();
	}
	Result.SetScale3D(FVector::OneVector);
	return Result;
}

#undef LOCTEXT_NAMESPACE
