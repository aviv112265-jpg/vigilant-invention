// MISHPACHA - asset-free rival crew brain.
#include "AI/MishpachaRivalAIController.h"
#include "Characters/MishpachaCharacter.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"

AMishpachaRivalAIController::AMishpachaRivalAIController()
{
}

void AMishpachaRivalAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	GuardSpot = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	GetWorldTimerManager().SetTimer(ThinkTimer, this, &AMishpachaRivalAIController::Think, ThinkInterval, true, FMath::FRandRange(0.f, ThinkInterval));
}

void AMishpachaRivalAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(ThinkTimer);
	SetFiring(false);
	Super::OnUnPossess();
}

APawn* AMishpachaRivalAIController::FindTarget() const
{
	const APawn* MyPawn = GetPawn();
	APawn* Best = nullptr;
	double BestDistSq = FMath::Square(AggroRadius);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Candidate = It->Get() ? It->Get()->GetPawn() : nullptr;
		const AMishpachaCharacter* AsCharacter = Cast<AMishpachaCharacter>(Candidate);
		if (!Candidate || (AsCharacter && AsCharacter->IsDead()))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), MyPawn->GetActorLocation());
		if (DistSq < BestDistSq && CanSee(Candidate))
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

bool AMishpachaRivalAIController::CanSee(const AActor* Other) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || !Other)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RivalSight), false, MyPawn);
	Params.AddIgnoredActor(Other);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, MyPawn->GetPawnViewLocation(), Other->GetActorLocation(), ECC_Visibility, Params);
}

void AMishpachaRivalAIController::SetFiring(bool bNewFiring)
{
	if (bFiring == bNewFiring)
	{
		return;
	}
	bFiring = bNewFiring;
	if (AMishpachaCharacter* Gunman = Cast<AMishpachaCharacter>(GetPawn()))
	{
		if (bFiring)
		{
			Gunman->SetAiming(true);
			Gunman->StartFire();
		}
		else
		{
			Gunman->StopFire();
			Gunman->SetAiming(false);
		}
	}
}

void AMishpachaRivalAIController::Think()
{
	const AMishpachaCharacter* Gunman = Cast<AMishpachaCharacter>(GetPawn());
	if (!Gunman || Gunman->IsDead())
	{
		SetFiring(false);
		return;
	}

	APawn* CurrentTarget = Target.Get();
	if (!CurrentTarget || !CanSee(CurrentTarget) || FVector::Dist(CurrentTarget->GetActorLocation(), Gunman->GetActorLocation()) > AggroRadius * 1.3f)
	{
		CurrentTarget = FindTarget();
		Target = CurrentTarget;
	}

	if (!CurrentTarget)
	{
		SetFiring(false);
		ClearFocus(EAIFocusPriority::Gameplay);
		if (FVector::Dist2D(Gunman->GetActorLocation(), GuardSpot) > 300.f)
		{
			MoveToLocation(GuardSpot, 150.f);
		}
		return;
	}

	SetFocus(CurrentTarget, EAIFocusPriority::Gameplay);
	if (FVector::Dist(CurrentTarget->GetActorLocation(), Gunman->GetActorLocation()) <= ShootDistance)
	{
		// Stand and shoot (semi-auto: re-trigger every think).
		StopMovement();
		SetFiring(false);
		SetFiring(true);
	}
	else
	{
		SetFiring(false);
		MoveToActor(CurrentTarget, ShootDistance * 0.6f);
	}
}
