// MISHPACHA - asset-free pedestrian brain.
#include "AI/MishpachaCivilianAIController.h"
#include "Characters/MishpachaCharacter.h"
#include "World/MishpachaWorldEvents.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

AMishpachaCivilianAIController::AMishpachaCivilianAIController()
{
}

void AMishpachaCivilianAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	HomeLocation = InPawn ? InPawn->GetActorLocation() : FVector::ZeroVector;
	Unsubscribe();
	GunshotHandle = FMishpachaWorldEvents::OnGunshot().AddUObject(this, &AMishpachaCivilianAIController::HandleGunshot);
	GetWorldTimerManager().SetTimer(WanderTimer, this, &AMishpachaCivilianAIController::Wander, 4.f, true, FMath::FRandRange(0.2f, 3.f));
}

void AMishpachaCivilianAIController::OnUnPossess()
{
	Unsubscribe();
	GetWorldTimerManager().ClearTimer(WanderTimer);
	Super::OnUnPossess();
}

void AMishpachaCivilianAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Unsubscribe();
	Super::EndPlay(EndPlayReason);
}

void AMishpachaCivilianAIController::Unsubscribe()
{
	if (GunshotHandle.IsValid())
	{
		FMishpachaWorldEvents::OnGunshot().Remove(GunshotHandle);
		GunshotHandle.Reset();
	}
}

void AMishpachaCivilianAIController::Wander()
{
	AMishpachaCharacter* Civilian = Cast<AMishpachaCharacter>(GetPawn());
	if (!Civilian || Civilian->IsDead())
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < FleeUntil)
	{
		return;
	}
	Civilian->SetSprinting(false);

	if (FMath::FRand() < 0.3f)
	{
		return; // stand around for a bit
	}
	const FVector2D Offset = FMath::RandPointInCircle(WanderRadius);
	MoveToLocation(HomeLocation + FVector(Offset.X, Offset.Y, 0.f), 80.f);
}

void AMishpachaCivilianAIController::HandleGunshot(UWorld* World, const FVector& Location)
{
	AMishpachaCharacter* Civilian = Cast<AMishpachaCharacter>(GetPawn());
	if (World != GetWorld() || !Civilian || Civilian->IsDead())
	{
		return;
	}

	FVector Away = Civilian->GetActorLocation() - Location;
	Away.Z = 0.f;
	if (Away.SizeSquared() > FMath::Square(FleeHearingRadius))
	{
		return;
	}

	FleeUntil = World->GetTimeSeconds() + FleeDuration;
	Civilian->SetSprinting(true);
	const FVector Direction = Away.IsNearlyZero() ? FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal() : Away.GetSafeNormal();
	MoveToLocation(Civilian->GetActorLocation() + Direction * 3000.f, 100.f);
}
