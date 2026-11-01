// MISHPACHA - helpers for reporting crimes and witness checks.
#include "Heat/CrimeLibrary.h"
#include "Heat/HeatComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

bool UCrimeLibrary::IsWitnessed(const UObject* WorldContextObject, FVector Location, AActor* Offender, float Radius)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return false;
	}

	static const FName CivilianTag(TEXT("Civilian"));
	static const FName PoliceTag(TEXT("Police"));
	const double RadiusSq = FMath::Square(static_cast<double>(Radius));

	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Witness = *It;
		if (!Witness || Witness == Offender || (!Witness->ActorHasTag(CivilianTag) && !Witness->ActorHasTag(PoliceTag)))
		{
			continue;
		}

		const FVector EyeLocation = Witness->GetPawnViewLocation();
		if (FVector::DistSquared(EyeLocation, Location) > RadiusSq)
		{
			continue;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(CrimeWitness), false, Witness);
		if (Offender)
		{
			Params.AddIgnoredActor(Offender);
			if (AActor* Parent = Offender->GetAttachParentActor())
			{
				Params.AddIgnoredActor(Parent);
			}
		}

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, EyeLocation, Location, ECC_Visibility, Params))
		{
			return true;
		}
	}
	return false;
}

void UCrimeLibrary::ReportCrimeByActor(AActor* Offender, ECrimeType Crime, FVector Location, bool bAlwaysWitnessed)
{
	if (!Offender || !Offender->HasAuthority())
	{
		return;
	}

	UHeatComponent* Heat = FindHeatComponent(Offender);
	if (!Heat)
	{
		return;
	}

	const bool bWitnessed = bAlwaysWitnessed || IsWitnessed(Offender, Location, Offender);
	Heat->ReportCrime(Crime, Location, bWitnessed);
}

UHeatComponent* UCrimeLibrary::FindHeatComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	const APlayerState* PlayerState = nullptr;
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		PlayerState = Pawn->GetPlayerState();
	}
	else if (const AController* Controller = Cast<AController>(Actor))
	{
		PlayerState = Controller->PlayerState;
	}
	else if (const APlayerState* AsPlayerState = Cast<APlayerState>(Actor))
	{
		PlayerState = AsPlayerState;
	}

	if (PlayerState)
	{
		return PlayerState->FindComponentByClass<UHeatComponent>();
	}

	// Weapons, projectiles, etc. -> walk up the owner chain.
	const AActor* OwnerActor = Actor->GetOwner();
	return (OwnerActor && OwnerActor != Actor) ? FindHeatComponent(OwnerActor) : nullptr;
}
