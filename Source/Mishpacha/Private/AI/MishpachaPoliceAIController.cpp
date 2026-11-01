// MISHPACHA - asset-free police officer brain.
#include "AI/MishpachaPoliceAIController.h"
#include "Characters/MishpachaCharacter.h"
#include "Heat/HeatComponent.h"
#include "Heat/CrimeLibrary.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"

AMishpachaPoliceAIController::AMishpachaPoliceAIController()
{
}

void AMishpachaPoliceAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AMishpachaPoliceAIController::Think, FMath::Max(ThinkInterval, 0.1f), true, FMath::FRandRange(0.f, 0.3f));
}

void AMishpachaPoliceAIController::OnUnPossess()
{
	SetFiring(false);
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	Super::OnUnPossess();
}

void AMishpachaPoliceAIController::SetTarget(APawn* NewTarget)
{
	Target = NewTarget;
}

APawn* AMishpachaPoliceAIController::FindNearestWantedPlayer() const
{
	const APawn* MyPawn = GetPawn();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!MyPawn || !GameState)
	{
		return nullptr;
	}

	APawn* Best = nullptr;
	double BestDistSq = FMath::Square(30000.0);
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		APawn* Candidate = PlayerState ? PlayerState->GetPawn() : nullptr;
		const UHeatComponent* Heat = PlayerState ? PlayerState->FindComponentByClass<UHeatComponent>() : nullptr;
		if (!Candidate || !Heat || Heat->GetHeatLevel() <= 0)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), MyPawn->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

bool AMishpachaPoliceAIController::CanSee(const AActor* Other) const
{
	const APawn* MyPawn = GetPawn();
	const UWorld* World = GetWorld();
	if (!MyPawn || !Other || !World)
	{
		return false;
	}
	if (FVector::DistSquared(MyPawn->GetActorLocation(), Other->GetActorLocation()) > FMath::Square(SightDistance))
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PoliceAISight), false, MyPawn);
	Params.AddIgnoredActor(Other);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, MyPawn->GetPawnViewLocation(), Other->GetActorLocation(), ECC_Visibility, Params);
}

void AMishpachaPoliceAIController::SetFiring(bool bNewFiring)
{
	if (bFiring == bNewFiring)
	{
		return;
	}
	bFiring = bNewFiring;

	if (AMishpachaCharacter* Officer = Cast<AMishpachaCharacter>(GetPawn()))
	{
		if (bFiring)
		{
			Officer->SetAiming(true);
			Officer->StartFire();
		}
		else
		{
			Officer->StopFire();
			Officer->SetAiming(false);
		}
	}
}

void AMishpachaPoliceAIController::Think()
{
	AMishpachaCharacter* Officer = Cast<AMishpachaCharacter>(GetPawn());
	if (!Officer || Officer->IsDead())
	{
		SetFiring(false);
		return;
	}

	APawn* CurrentTarget = Target.Get();
	UHeatComponent* Heat = UCrimeLibrary::FindHeatComponent(CurrentTarget);
	if (!CurrentTarget || !Heat || Heat->GetHeatLevel() <= 0)
	{
		CurrentTarget = FindNearestWantedPlayer();
		Target = CurrentTarget;
		Heat = UCrimeLibrary::FindHeatComponent(CurrentTarget);
	}

	if (!CurrentTarget || !Heat || Heat->GetHeatLevel() <= 0)
	{
		// Stand down.
		SetFiring(false);
		ClearFocus(EAIFocusPriority::Gameplay);
		StopMovement();
		return;
	}

	if (CanSee(CurrentTarget))
	{
		Heat->NotifySpottedByPolice(CurrentTarget->GetActorLocation());
		SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);

		const float Distance = FVector::Dist(CurrentTarget->GetActorLocation(), Officer->GetActorLocation());
		const bool bShoot = Heat->GetHeatLevel() >= ShootFromHeatLevel && Distance <= ShootDistance;
		if (bShoot)
		{
			// Stand and shoot (semi-auto pistols: re-trigger every think).
			StopMovement();
			SetFiring(false);
			SetFiring(true);
		}
		else
		{
			SetFiring(false);
			MoveToActor(CurrentTarget, 400.f);
		}
	}
	else
	{
		SetFiring(false);
		ClearFocus(EAIFocusPriority::Gameplay);
		MoveToLocation(Heat->GetLastKnownLocation(), 300.f);
	}
}
