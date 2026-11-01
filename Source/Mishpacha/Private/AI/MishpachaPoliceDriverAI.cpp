// MISHPACHA - police car driver.
#include "AI/MishpachaPoliceDriverAI.h"
#include "Vehicles/MishpachaPoliceCar.h"
#include "Heat/CrimeLibrary.h"
#include "Heat/HeatComponent.h"
#include "World/CityGreyboxGenerator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace PoliceDriving
{
	static constexpr float TickInterval = 0.1f;
}

AMishpachaPoliceDriverAI::AMishpachaPoliceDriverAI()
{
	bWantsPlayerState = false;
}

void AMishpachaPoliceDriverAI::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	GetWorldTimerManager().SetTimer(DriveTimer, this, &AMishpachaPoliceDriverAI::Drive, PoliceDriving::TickInterval, true);
}

void AMishpachaPoliceDriverAI::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(DriveTimer);
	Super::OnUnPossess();
}

APawn* AMishpachaPoliceDriverAI::FindSuspect(const AMishpachaPoliceCar* Car) const
{
	APawn* Best = nullptr;
	double BestDistSq = FMath::Square(40000.0);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* Candidate = It->Get() ? It->Get()->GetPawn() : nullptr;
		const UHeatComponent* Heat = UCrimeLibrary::FindHeatComponent(Candidate);
		if (!Candidate || !Heat || Heat->GetHeatLevel() <= 0)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), Car->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

FVector AMishpachaPoliceDriverAI::GetLookAheadPoint(const FVector& From) const
{
	if (Route.Num() < 2)
	{
		return Route.Num() == 1 ? Route[0] : From;
	}
	// Closest point on the route, then walk LookAhead further along it.
	int32 BestSegment = 0;
	double BestDistSq = TNumericLimits<double>::Max();
	double BestT = 0.0;
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		const FVector A = Route[Index - 1];
		const FVector B = Route[Index];
		const FVector AB = B - A;
		const double T = FMath::Clamp(FVector::DotProduct(From - A, AB) / FMath::Max(AB.SizeSquared(), 1.0), 0.0, 1.0);
		const double DistSq = FVector::DistSquared2D(From, A + AB * T);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestSegment = Index;
			BestT = T;
		}
	}
	double Remaining = LookAhead;
	FVector Point = FMath::Lerp(Route[BestSegment - 1], Route[BestSegment], BestT);
	for (int32 Index = BestSegment; Index < Route.Num(); ++Index)
	{
		const double Leg = FVector::Dist2D(Point, Route[Index]);
		if (Leg >= Remaining)
		{
			return Point + (Route[Index] - Point).GetSafeNormal2D() * Remaining;
		}
		Remaining -= Leg;
		Point = Route[Index];
	}
	return Route.Last();
}

void AMishpachaPoliceDriverAI::Park(AMishpachaPoliceCar* Car)
{
	Car->SetThrottle(0.f);
	Car->SetSteering(0.f);
	Car->SetBrake(0.f);
	Car->SetHandbrake(true);
}

void AMishpachaPoliceDriverAI::Drive()
{
	AMishpachaPoliceCar* Car = Cast<AMishpachaPoliceCar>(GetPawn());
	if (!Car)
	{
		return;
	}

	if (Car->bRoadblock)
	{
		Park(Car);
		Car->DeployOfficers(FindSuspect(Car));
		return;
	}

	APawn* Suspect = FindSuspect(Car);
	if (!Suspect || Car->HasDeployed())
	{
		Park(Car);
		return;
	}

	const FVector CarLocation = Car->GetActorLocation();
	if (FVector::Dist(CarLocation, Suspect->GetActorLocation()) < DeployDistance)
	{
		Park(Car);
		if (Car->GetVelocity().Size() < 300.f)
		{
			Car->DeployOfficers(Suspect);
		}
		return;
	}

	// Re-plan every second (the suspect moves).
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastPlanTime > 1.0)
	{
		LastPlanTime = Now;
		Route.Reset();
		const ACityGreyboxGenerator* Grid = TActorIterator<ACityGreyboxGenerator>(GetWorld()) ? *TActorIterator<ACityGreyboxGenerator>(GetWorld()) : nullptr;
		if (!Grid || !Grid->FindRoadRoute(CarLocation, Suspect->GetActorLocation(), Route))
		{
			Route = { CarLocation, Suspect->GetActorLocation() };
		}
	}

	// Steering toward the look-ahead point (local space: +X forward, +Y right).
	const FVector Local = Car->GetActorTransform().InverseTransformPositionNoScale(GetLookAheadPoint(CarLocation));
	const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	const float Speed = Car->GetVelocity().Size();

	if (ReverseTimeLeft > 0.f)
	{
		// Backing out of a wall: reverse with the wheel turned the other way.
		ReverseTimeLeft -= PoliceDriving::TickInterval;
		Car->SetHandbrake(false);
		Car->SetThrottle(0.f);
		Car->SetBrake(1.f);
		Car->SetSteering(-FMath::Sign(Angle));
		return;
	}

	const float Desired = FMath::Abs(Angle) > 35.f ? TurnSpeed : CruiseSpeed;
	Car->SetHandbrake(false);
	Car->SetSteering(FMath::Clamp(Angle / 30.f, -1.f, 1.f));
	Car->SetThrottle(Speed < Desired ? (FMath::Abs(Angle) > 90.f ? 0.4f : 0.85f) : 0.f);
	Car->SetBrake(Speed > Desired + 400.f ? 0.7f : 0.f);

	// Stuck (wall, parked car): pushing but not moving for a while -> back out.
	StuckTime = (Speed < 80.f) ? StuckTime + PoliceDriving::TickInterval : 0.f;
	if (StuckTime > 2.5f)
	{
		StuckTime = 0.f;
		ReverseTimeLeft = 1.6f;
	}
}
