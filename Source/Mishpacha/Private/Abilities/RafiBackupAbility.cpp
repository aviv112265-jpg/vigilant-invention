// MISHPACHA - Rafi's ability: "Call the boys".
#include "Abilities/RafiBackupAbility.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaAbility"

URafiBackupAbility::URafiBackupAbility()
{
	AbilityName = LOCTEXT("RafiAbilityName", "Call the Boys");
	Cooldown = 90.f;
	Duration = 60.f;
}

bool URafiBackupAbility::CanActivate(FText& OutReason) const
{
	if (!Super::CanActivate(OutReason))
	{
		return false;
	}
	if (!BackupPawnClass)
	{
		OutReason = LOCTEXT("RafiNoClass", "No backup pawn class set on Rafi's ability.");
		return false;
	}
	return true;
}

void URafiBackupAbility::ActivateAbility()
{
	APawn* Rafi = GetOwnerPawn();
	UWorld* World = GetWorld();
	if (!Rafi || !World)
	{
		EndAbility();
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 Index = 0; Index < NumBackup; ++Index)
	{
		const float Angle = (UE_TWO_PI / NumBackup) * Index;
		FVector SpawnLocation = Rafi->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * SpawnRadius;

		FNavLocation NavLocation;
		if (NavSys && NavSys->ProjectPointToNavigation(SpawnLocation, NavLocation, FVector(500.f, 500.f, 1000.f)))
		{
			SpawnLocation = NavLocation.Location + FVector(0.f, 0.f, 100.f);
		}

		APawn* Soldier = World->SpawnActor<APawn>(BackupPawnClass, SpawnLocation, Rafi->GetActorRotation(), Params);
		if (!Soldier)
		{
			continue;
		}
		Soldier->Tags.AddUnique(FName(TEXT("FamilyBackup")));
		if (!Soldier->GetController())
		{
			Soldier->SpawnDefaultController();
		}
		SpawnedBackup.Add(Soldier);
	}

	NotifyOwner(LOCTEXT("RafiCalled", "Rafi made a call. The boys are on their way."));
	World->GetTimerManager().SetTimer(FollowTimer, this, &URafiBackupAbility::UpdateFollow, 1.f, true, 0.1f);
}

void URafiBackupAbility::UpdateFollow()
{
	APawn* Rafi = GetOwnerPawn();
	if (!Rafi)
	{
		return;
	}

	for (APawn* Soldier : SpawnedBackup)
	{
		if (AAIController* AI = Soldier ? Cast<AAIController>(Soldier->GetController()) : nullptr)
		{
			AI->MoveToActor(Rafi, FollowDistance);
		}
	}
}

void URafiBackupAbility::DeactivateAbility()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FollowTimer);
	}

	for (APawn* Soldier : SpawnedBackup)
	{
		if (!IsValid(Soldier))
		{
			continue;
		}
		if (AController* SoldierController = Soldier->GetController())
		{
			SoldierController->Destroy();
		}
		Soldier->Destroy();
	}
	SpawnedBackup.Reset();
}

#undef LOCTEXT_NAMESPACE
