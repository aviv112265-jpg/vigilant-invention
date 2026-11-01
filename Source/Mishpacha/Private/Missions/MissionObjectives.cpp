// MISHPACHA - built-in objective types.
#include "Missions/MissionObjectives.h"
#include "Characters/MishpachaCharacter.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Core/MishpachaPlayerState.h"
#include "Heat/HeatComponent.h"
#include "Mishpacha.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

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
