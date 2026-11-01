// MISHPACHA - built-in objective types.
#include "Missions/MissionObjectives.h"
#include "Characters/MishpachaCharacter.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Core/MishpachaPlayerState.h"
#include "Heat/HeatComponent.h"
#include "Mishpacha.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

// ----- Reach Location -----

void UObjective_ReachLocation::Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer)
{
	Super::Activate(InMissions, InPlayer);

	TargetActor = nullptr;
	if (!TargetActorTag.IsNone())
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsWithTag(this, TargetActorTag, Found);
		if (Found.Num() > 0)
		{
			TargetActor = Found[0];
		}
		else
		{
			UE_LOG(LogMishpacha, Warning, TEXT("ReachLocation: no actor tagged '%s' - using TargetLocation."), *TargetActorTag.ToString());
		}
	}
}

FVector UObjective_ReachLocation::GetDestination() const
{
	const AActor* Target = TargetActor.Get();
	return Target ? Target->GetActorLocation() : TargetLocation;
}

bool UObjective_ReachLocation::GetGuideLocation(FVector& OutLocation) const
{
	OutLocation = GetDestination();
	return true;
}

void UObjective_ReachLocation::TickObjective(float DeltaTime)
{
	const APawn* Pawn = GetPlayerPawn();
	if (Pawn && FVector::Dist(Pawn->GetActorLocation(), GetDestination()) <= Radius)
	{
		MarkComplete();
	}
}

// ----- Enter Vehicle -----

void UObjective_EnterVehicle::TickObjective(float DeltaTime)
{
	const AMishpachaVehicle* Vehicle = Cast<AMishpachaVehicle>(GetPlayerPawn());
	if (Vehicle && (VehicleTag.IsNone() || Vehicle->ActorHasTag(VehicleTag)))
	{
		MarkComplete();
	}
}

bool UObjective_EnterVehicle::GetGuideLocation(FVector& OutLocation) const
{
	const APawn* Pawn = GetPlayerPawn();
	UWorld* World = GetWorld();
	if (!Pawn || !World || Cast<AMishpachaVehicle>(Pawn))
	{
		return false;
	}

	double BestDistSq = TNumericLimits<double>::Max();
	for (TActorIterator<AMishpachaVehicle> It(World); It; ++It)
	{
		if (!VehicleTag.IsNone() && !It->ActorHasTag(VehicleTag))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(It->GetActorLocation(), Pawn->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutLocation = It->GetActorLocation();
		}
	}
	return BestDistSq < TNumericLimits<double>::Max();
}

// ----- Eliminate Targets -----

void UObjective_EliminateTargets::Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer)
{
	Super::Activate(InMissions, InPlayer);

	Targets.Reset();
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsWithTag(this, TargetTag, Found);
	for (AActor* Actor : Found)
	{
		Targets.Add(Actor);
	}

	Required = RequiredCount > 0 ? FMath::Min(RequiredCount, Targets.Num()) : Targets.Num();
	if (Required == 0)
	{
		UE_LOG(LogMishpacha, Warning, TEXT("EliminateTargets: no actors tagged '%s' - objective auto-completes."), *TargetTag.ToString());
		MarkComplete();
	}
}

void UObjective_EliminateTargets::TickObjective(float DeltaTime)
{
	int32 Eliminated = 0;
	for (const TWeakObjectPtr<AActor>& Weak : Targets)
	{
		const AActor* Target = Weak.Get();
		const AMishpachaCharacter* TargetCharacter = Cast<AMishpachaCharacter>(Target);
		if (!IsValid(Target) || (TargetCharacter && TargetCharacter->IsDead()))
		{
			++Eliminated;
		}
	}

	if (Eliminated >= Required)
	{
		MarkComplete();
	}
}

bool UObjective_EliminateTargets::GetGuideLocation(FVector& OutLocation) const
{
	const APawn* Pawn = GetPlayerPawn();
	const FVector From = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<AActor>& Weak : Targets)
	{
		const AActor* Target = Weak.Get();
		const AMishpachaCharacter* TargetCharacter = Cast<AMishpachaCharacter>(Target);
		if (!IsValid(Target) || (TargetCharacter && TargetCharacter->IsDead()))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Target->GetActorLocation(), From);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutLocation = Target->GetActorLocation();
		}
	}
	return BestDistSq < TNumericLimits<double>::Max();
}

// ----- Rival Crew -----

UObjective_RivalCrew::UObjective_RivalCrew()
{
	RivalClass = AMishpachaRival::StaticClass();
}

static FVector FindTaggedLocation(const UObject* WorldContext, FName Tag, const FVector& Fallback)
{
	TArray<AActor*> Found;
	if (!Tag.IsNone())
	{
		UGameplayStatics::GetAllActorsWithTag(WorldContext, Tag, Found);
	}
	return Found.Num() > 0 ? Found[0]->GetActorLocation() : Fallback;
}

void UObjective_RivalCrew::Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer)
{
	Super::Activate(InMissions, InPlayer);

	Crew.Reset();
	UWorld* World = GetWorld();
	const APawn* Pawn = GetPlayerPawn();
	CrewSpot = FindTaggedLocation(this, LocationTag, Pawn ? Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 3000.f : FVector::ZeroVector);
	if (!World || !RivalClass)
	{
		MarkComplete();
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = 2.f * PI * Index / FMath::Max(1, Count);
		const FVector Spot = CrewSpot + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * SpreadRadius + FVector(0.f, 0.f, 60.f);
		const FRotator FacingOut(0.f, FMath::RadiansToDegrees(Angle), 0.f);
		if (APawn* Rival = World->SpawnActor<APawn>(RivalClass, Spot, FacingOut, Params))
		{
			Crew.Add(Rival);
		}
	}
	UE_LOG(LogMishpacha, Log, TEXT("RivalCrew: %d rivals at %s."), Crew.Num(), *CrewSpot.ToString());
	if (Crew.Num() == 0)
	{
		MarkComplete();
	}
}

void UObjective_RivalCrew::TickObjective(float DeltaTime)
{
	for (const TWeakObjectPtr<APawn>& Member : Crew)
	{
		const AMishpachaCharacter* AsCharacter = Cast<AMishpachaCharacter>(Member.Get());
		if (IsValid(Member.Get()) && !(AsCharacter && AsCharacter->IsDead()))
		{
			return;
		}
	}
	MarkComplete();
}

bool UObjective_RivalCrew::GetGuideLocation(FVector& OutLocation) const
{
	OutLocation = CrewSpot;
	return true;
}

// ----- Switch To Sibling -----

void UObjective_SwitchToSibling::TickObjective(float DeltaTime)
{
	const AMishpachaCharacter* AsCharacter = Cast<AMishpachaCharacter>(GetPlayerPawn());
	if (AsCharacter && AsCharacter->GetSiblingId() == SiblingId)
	{
		MarkComplete();
	}
}

// ----- Wait At -----

void UObjective_WaitAt::Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer)
{
	Super::Activate(InMissions, InPlayer);
	const APawn* Pawn = GetPlayerPawn();
	Spot = FindTaggedLocation(this, LocationTag, Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector);
	Waited = 0.f;
}

void UObjective_WaitAt::TickObjective(float DeltaTime)
{
	const APawn* Pawn = GetPlayerPawn();
	if (Pawn && FVector::Dist2D(Pawn->GetActorLocation(), Spot) <= Radius)
	{
		Waited += DeltaTime;
		if (Waited >= Seconds)
		{
			MarkComplete();
		}
	}
}

bool UObjective_WaitAt::GetGuideLocation(FVector& OutLocation) const
{
	OutLocation = Spot;
	return true;
}

// ----- Lose Heat -----

void UObjective_LoseHeat::TickObjective(float DeltaTime)
{
	const APlayerController* PC = Player.Get();
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	const UHeatComponent* Heat = PS ? PS->GetHeatComponent() : nullptr;
	if (Heat && Heat->GetHeatLevel() == 0)
	{
		MarkComplete();
	}
}
