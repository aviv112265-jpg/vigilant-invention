// MISHPACHA - Omer's ability: deploy his drone.
#include "Abilities/OmerDroneAbility.h"
#include "Abilities/DronePawn.h"
#include "Characters/MishpachaCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "MishpachaAbility"

UOmerDroneAbility::UOmerDroneAbility()
{
	AbilityName = LOCTEXT("OmerAbilityName", "Drone");
	Cooldown = 45.f;
	Duration = 40.f;
	DroneClass = ADronePawn::StaticClass();
}

bool UOmerDroneAbility::CanActivate(FText& OutReason) const
{
	if (!Super::CanActivate(OutReason))
	{
		return false;
	}

	const AMishpachaCharacter* Omer = Cast<AMishpachaCharacter>(GetOwner());
	if (!Omer || !Cast<APlayerController>(Omer->GetController()))
	{
		OutReason = LOCTEXT("DroneNoPlayer", "Only a player can fly the drone.");
		return false;
	}
	if (Omer->GetCurrentVehicle())
	{
		OutReason = LOCTEXT("DroneInVehicle", "Get out of the vehicle to launch the drone.");
		return false;
	}
	if (!DroneClass)
	{
		OutReason = LOCTEXT("DroneNoClass", "No drone class set on Omer's ability.");
		return false;
	}
	return true;
}

void UOmerDroneAbility::ActivateAbility()
{
	AMishpachaCharacter* Omer = Cast<AMishpachaCharacter>(GetOwner());
	APlayerController* PC = Omer ? Cast<APlayerController>(Omer->GetController()) : nullptr;
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		EndAbility();
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector SpawnLocation = Omer->GetActorLocation() + Omer->GetActorRotation().RotateVector(SpawnOffset);

	ADronePawn* Drone = World->SpawnActor<ADronePawn>(DroneClass, SpawnLocation, Omer->GetActorRotation(), Params);
	if (!Drone)
	{
		EndAbility();
		return;
	}

	Drone->InitDrone(this, Omer);
	ActiveDrone = Drone;
	Omer->StopFire();
	PC->Possess(Drone);
}

void UOmerDroneAbility::DeactivateAbility()
{
	ADronePawn* Drone = ActiveDrone.Get();
	ActiveDrone = nullptr;
	if (!Drone)
	{
		return;
	}

	AMishpachaCharacter* Omer = Cast<AMishpachaCharacter>(GetOwner());
	AController* DroneController = Drone->GetController();
	if (DroneController)
	{
		if (Omer && !Omer->IsDead())
		{
			// Possess() cleanly unpossesses the drone first.
			DroneController->Possess(Omer);
		}
		else
		{
			DroneController->UnPossess();
		}
	}
	Drone->Destroy();
}

#undef LOCTEXT_NAMESPACE
