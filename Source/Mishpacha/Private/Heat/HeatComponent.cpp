// MISHPACHA - "Heat" (Hom), the wanted system.
#include "Heat/HeatComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UHeatComponent::UHeatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
	SetIsReplicatedByDefault(true);

	LevelThresholds = { 10.f, 40.f, 90.f, 160.f, 250.f };

	CrimePoints.Add(ECrimeType::PropertyDamage, 5.f);
	CrimePoints.Add(ECrimeType::Assault, 15.f);
	CrimePoints.Add(ECrimeType::Gunshot, 12.f);
	CrimePoints.Add(ECrimeType::VehicleTheft, 12.f);
	CrimePoints.Add(ECrimeType::Carjacking, 25.f);
	CrimePoints.Add(ECrimeType::Murder, 45.f);
	CrimePoints.Add(ECrimeType::CopKilled, 100.f);
}

void UHeatComponent::BeginPlay()
{
	Super::BeginPlay();

	// Only the server simulates the escape timer.
	if (!IsServer())
	{
		SetComponentTickEnabled(false);
	}
}

void UHeatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (HeatLevel <= 0)
	{
		return;
	}

	TimeSinceSpotted += DeltaTime;
	if (TimeSinceSpotted >= GetSearchDuration())
	{
		ClearHeat();
	}
}

void UHeatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UHeatComponent, HeatLevel);
	DOREPLIFETIME(UHeatComponent, LastKnownLocation);
	DOREPLIFETIME(UHeatComponent, TimeSinceSpotted);
}

void UHeatComponent::ReportCrime(ECrimeType Crime, FVector Location, bool bWitnessed)
{
	if (!IsServer())
	{
		return;
	}

	const float BasePoints = CrimePoints.FindRef(Crime);
	const float Multiplier = bWitnessed ? 1.f : (Crime == ECrimeType::Gunshot ? AudibleCrimeMultiplier : 0.f);
	const float Points = BasePoints * Multiplier;
	if (Points <= 0.f)
	{
		return;
	}

	HeatPoints += Points;
	LastKnownLocation = Location;
	TimeSinceSpotted = 0.f;
	SetHeatLevel(FMath::Max(HeatLevel, ComputeLevelFromPoints()));
}

void UHeatComponent::NotifySpottedByPolice(FVector SpottedLocation)
{
	if (!IsServer() || HeatLevel <= 0)
	{
		return;
	}

	TimeSinceSpotted = 0.f;
	LastKnownLocation = SpottedLocation;
}

void UHeatComponent::ReduceHeatLevels(int32 Levels)
{
	if (!IsServer() || Levels <= 0)
	{
		return;
	}

	const int32 NewLevel = FMath::Max(0, HeatLevel - Levels);
	HeatPoints = (NewLevel > 0 && LevelThresholds.IsValidIndex(NewLevel - 1)) ? LevelThresholds[NewLevel - 1] : 0.f;
	TimeSinceSpotted = 0.f;
	SetHeatLevel(NewLevel);
}

void UHeatComponent::ClearHeat()
{
	if (!IsServer())
	{
		return;
	}

	HeatPoints = 0.f;
	TimeSinceSpotted = 0.f;
	SetHeatLevel(0);
}

void UHeatComponent::DebugSetHeatLevel(int32 Level, const FVector& SeenAt)
{
	if (!IsServer())
	{
		return;
	}
	Level = FMath::Clamp(Level, 0, MaxHeatLevel);
	HeatPoints = (Level > 0 && LevelThresholds.IsValidIndex(Level - 1)) ? LevelThresholds[Level - 1] : 0.f;
	TimeSinceSpotted = 0.f;
	LastKnownLocation = SeenAt;
	SetHeatLevel(Level);
}

float UHeatComponent::GetSearchDuration() const
{
	return BaseSearchDuration + SearchDurationPerLevel * static_cast<float>(FMath::Max(0, HeatLevel - 1));
}

float UHeatComponent::GetEscapeProgress() const
{
	const float SearchDuration = GetSearchDuration();
	if (HeatLevel <= 0 || SearchDuration <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(TimeSinceSpotted / SearchDuration, 0.f, 1.f);
}

void UHeatComponent::OnRep_HeatLevel(int32 OldLevel)
{
	OnHeatChanged.Broadcast(HeatLevel, OldLevel);
}

void UHeatComponent::SetHeatLevel(int32 NewLevel)
{
	const int32 ClampedLevel = FMath::Clamp(NewLevel, 0, MaxHeatLevel);
	if (ClampedLevel == HeatLevel)
	{
		return;
	}

	const int32 OldLevel = HeatLevel;
	HeatLevel = ClampedLevel;
	OnHeatChanged.Broadcast(HeatLevel, OldLevel);

	// PlayerStates replicate slowly by default; push the change now.
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
}

int32 UHeatComponent::ComputeLevelFromPoints() const
{
	int32 Level = 0;
	for (const float Threshold : LevelThresholds)
	{
		if (HeatPoints >= Threshold)
		{
			++Level;
		}
		else
		{
			break;
		}
	}
	return FMath::Min(Level, MaxHeatLevel);
}

bool UHeatComponent::IsServer() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->HasAuthority();
}
