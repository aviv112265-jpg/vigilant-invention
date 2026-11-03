// MISHPACHA - civilian driver.
#include "AI/MishpachaTrafficAI.h"
#include "Mishpacha.h"
#include "Vehicles/MishpachaVehicle.h"
#include "World/CityGreyboxGenerator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

namespace TrafficDriving
{
	static constexpr float TickInterval = 0.1f;
}

AMishpachaTrafficAI::AMishpachaTrafficAI()
{
	bWantsPlayerState = false;
}

void AMishpachaTrafficAI::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	for (TActorIterator<ACityGreyboxGenerator> It(GetWorld()); It; ++It)
	{
		Grid = *It;
		break;
	}
	GetWorldTimerManager().SetTimer(DriveTimer, this, &AMishpachaTrafficAI::Drive, TrafficDriving::TickInterval, true, FMath::FRandRange(0.f, 0.1f));
}

void AMishpachaTrafficAI::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(DriveTimer);
	Super::OnUnPossess();
}

void AMishpachaTrafficAI::SetLeg(const FIntPoint& From, const FIntPoint& To)
{
	FromNode = From;
	ToNode = To;
}

FVector AMishpachaTrafficAI::LanePoint(const FIntPoint& Node, const FVector& Direction) const
{
	// Right of the direction of travel: (x, y) -> (-y, x).
	const FVector Right(-Direction.Y, Direction.X, 0.f);
	return Grid->GetIntersection(Node.X, Node.Y) + Right * LaneOffset;
}

void AMishpachaTrafficAI::PickNextNode()
{
	const ACityGreyboxGenerator* City = Grid.Get();
	TArray<FIntPoint> Options;
	for (const FIntPoint& Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
	{
		const FIntPoint Next = ToNode + Step;
		if (Next != FromNode && Next.X >= 0 && Next.Y >= 0 && Next.X <= City->GetBlocksX() && Next.Y <= City->GetBlocksY())
		{
			Options.Add(Next);
		}
	}
	const FIntPoint Next = Options.Num() > 0 ? Options[FMath::RandRange(0, Options.Num() - 1)] : FromNode;
	FromNode = ToNode;
	ToNode = Next;
}

bool AMishpachaTrafficAI::IsBlockedAhead(const AMishpachaVehicle* Car, float Distance) const
{
	const FVector Forward = Car->GetActorForwardVector();
	const FVector Start = Car->GetActorLocation() + Forward * 280.f + FVector(0.f, 0.f, 60.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TrafficAhead), false, Car);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_Vehicle);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FHitResult Hit;
	return GetWorld()->SweepSingleByObjectType(Hit, Start, Start + Forward * Distance, FQuat::Identity, Objects, FCollisionShape::MakeSphere(110.f), Params);
}

void AMishpachaTrafficAI::Drive()
{
	AMishpachaVehicle* Car = Cast<AMishpachaVehicle>(GetPawn());
	if (!Car || !Grid.IsValid())
	{
		return;
	}

	const FVector Location = Car->GetActorLocation();
	FVector Direction = Grid->GetIntersection(ToNode.X, ToNode.Y) - Grid->GetIntersection(FromNode.X, FromNode.Y);
	Direction.Z = 0.f;
	Direction = Direction.GetSafeNormal();
	FVector Target = LanePoint(ToNode, Direction);

	// Close to the junction: on to the next leg (the lane point of the new leg pulls the car through the turn).
	if (FVector::Dist2D(Location, Target) < 650.f)
	{
		PickNextNode();
		Direction = (Grid->GetIntersection(ToNode.X, ToNode.Y) - Grid->GetIntersection(FromNode.X, FromNode.Y)).GetSafeNormal2D();
		Target = LanePoint(ToNode, Direction);
	}

	// Aim a little past the lane's start so the car merges into it smoothly.
	const FVector LaneStart = LanePoint(FromNode, Direction);
	const FVector Along = Direction * FMath::Clamp(FVector::DotProduct(Location - LaneStart, Direction) + 900.f, 0.f, FVector::Dist2D(LaneStart, Target));
	const FVector Aim = LaneStart + Along;
	const FVector Local = Car->GetActorTransform().InverseTransformPositionNoScale(Aim);
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	const float Speed = Car->GetVelocity().Size();

	if (ReverseTimeLeft > 0.f)
	{
		ReverseTimeLeft -= TrafficDriving::TickInterval;
		Car->SetHandbrake(false);
		Car->SetThrottle(0.f);
		Car->SetBrake(0.8f);
		Car->SetSteering(-FMath::Sign(Angle));
		return;
	}

	const float ToJunction = FVector::Dist2D(Location, Target);
	float Desired = ToJunction < 1600.f ? JunctionSpeed : CruiseSpeed;
	if (FMath::Abs(Angle) > 30.f)
	{
		Desired = FMath::Min(Desired, 450.f);
	}

	// Traffic light at the junction ahead: stop at the line on red, and on yellow when there's room to.
	bool bSignalStop = false;
	bool bAtRedLight = false;
	const FVector Junction = Grid->GetIntersection(ToNode.X, ToNode.Y);
	const int32 Signal = Grid->GetSignalColor(ToNode, ToNode.X != FromNode.X);
	const float ToLine = FVector::DotProduct(Junction - Location, Direction) - (Grid->GetRoadWidth() * 0.5f + 300.f);
	if ((Signal == 0 || Signal == 1) && ToLine > -100.f && ToLine < 4000.f && (Signal == 0 || ToLine > Speed * 0.9f))
	{
		bSignalStop = true;
		Desired = FMath::Min(Desired, FMath::Sqrt(2.f * 450.f * FMath::Max(0.f, ToLine - 50.f))); // ~0.45 g to a stop at the line
		bAtRedLight = ToLine < 150.f;
	}
	if (bAtRedLight && !bWaitingAtLight)
	{
		UE_LOG(LogMishpacha, Verbose, TEXT("Traffic: %s stopped at a %s light (%d, %d), %.0f cm from the line"), *Car->GetName(), Signal == 0 ? TEXT("red") : TEXT("yellow"), ToNode.X, ToNode.Y, ToLine);
	}
	bWaitingAtLight = bAtRedLight;

	const bool bBlocked = IsBlockedAhead(Car, 250.f + Speed * 0.9f);
	Car->SetHandbrake(false);
	Car->SetSteering(FMath::Clamp(Angle / 28.f, -1.f, 1.f));
	if (bBlocked || bAtRedLight)
	{
		Car->SetThrottle(0.f);
		Car->SetBrake(Speed > 50.f ? 1.f : 0.f); // stop (and don't creep into reverse)
		Car->SetHandbrake(Speed <= 50.f);
	}
	else
	{
		Car->SetThrottle(Speed < Desired ? 0.6f : 0.f);
		Car->SetBrake(Speed > Desired + (bSignalStop ? 100.f : 300.f) ? (bSignalStop ? 1.f : 0.5f) : 0.f);
	}

	// Stuck against something that isn't in the sweep (a wall, a parked car's corner), or waiting
	// behind something that never moves (a parked car, a crash): back up and try again.
	// Waiting at a red light (or in the queue behind one) isn't being stuck.
	StuckTime = (!bBlocked && !bSignalStop && Speed < 60.f) ? StuckTime + TrafficDriving::TickInterval : 0.f;
	BlockedTime = bBlocked ? BlockedTime + TrafficDriving::TickInterval : 0.f;
	const float MaxBlockedTime = Signal >= 0 && ToLine < 6000.f ? 20.f : 6.f;
	if (StuckTime > 3.f || BlockedTime > MaxBlockedTime)
	{
		StuckTime = 0.f;
		BlockedTime = 0.f;
		ReverseTimeLeft = 1.5f;
	}
}
