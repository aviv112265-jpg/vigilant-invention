// MISHPACHA - base AI controller with a "no navmesh" fallback.
#include "AI/MishpachaNPCAIController.h"
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

AMishpachaNPCAIController::AMishpachaNPCAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
}

FPathFollowingRequestResult AMishpachaNPCAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
	FPathFollowingRequestResult Result = Super::MoveTo(MoveRequest, OutPath);
	if (Result.Code != EPathFollowingRequestResult::Failed)
	{
		ClearDirectSteering();
		return Result;
	}

	// No path (usually: no navmesh) -> steer directly.
	SteerGoalActor = MoveRequest.IsMoveToActorRequest() ? MoveRequest.GetGoalActor() : nullptr;
	SteerGoalLocation = MoveRequest.GetGoalLocation();
	SteerAcceptance = FMath::Max(MoveRequest.GetAcceptanceRadius(), 50.f);
	SteerElapsed = 0.f;
	bDirectSteering = SteerGoalActor.IsValid() || MoveRequest.IsMoveToActorRequest() == false;
	return Result;
}

void AMishpachaNPCAIController::StopMovement()
{
	ClearDirectSteering();
	Super::StopMovement();
}

void AMishpachaNPCAIController::OnUnPossess()
{
	ClearDirectSteering();
	Super::OnUnPossess();
}

void AMishpachaNPCAIController::ClearDirectSteering()
{
	bDirectSteering = false;
	SteerGoalActor = nullptr;
	AvoidTimeLeft = 0.f;
}

void AMishpachaNPCAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* MyPawn = GetPawn();
	if (!bDirectSteering || !MyPawn)
	{
		return;
	}

	SteerElapsed += DeltaSeconds;
	const FVector Goal = SteerGoalActor.IsValid() ? SteerGoalActor->GetActorLocation() : SteerGoalLocation;
	FVector ToGoal = Goal - MyPawn->GetActorLocation();
	ToGoal.Z = 0.f;

	if (ToGoal.Size() <= SteerAcceptance + 60.f || SteerElapsed > DirectSteerTimeout)
	{
		ClearDirectSteering();
		return;
	}

	FVector Direction = ToGoal.GetSafeNormal();

	// Very small obstacle avoidance: if something blocks the way, slide sideways for a moment.
	UWorld* World = GetWorld();
	if (World)
	{
		const FVector Start = MyPawn->GetActorLocation();
		const FVector End = Start + Direction * 150.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NPCSteer), false, MyPawn);
		if (SteerGoalActor.IsValid())
		{
			Params.AddIgnoredActor(SteerGoalActor.Get());
		}
		FHitResult Hit;
		if (AvoidTimeLeft <= 0.f && World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(30.f), Params))
		{
			AvoidTimeLeft = 0.8f;
			AvoidSign = FMath::RandBool() ? 1.f : -1.f;
		}
	}

	if (AvoidTimeLeft > 0.f)
	{
		AvoidTimeLeft -= DeltaSeconds;
		Direction = Direction.RotateAngleAxis(70.f * AvoidSign, FVector::UpVector);
	}

	MyPawn->AddMovementInput(Direction, 1.f);
}
