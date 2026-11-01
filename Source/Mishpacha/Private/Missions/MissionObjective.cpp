// MISHPACHA - base mission objective.
#include "Missions/MissionObjective.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UMissionObjective::Activate(UMissionSubsystem* InMissions, APlayerController* InPlayer)
{
	Missions = InMissions;
	Player = InPlayer;
	bComplete = false;
}

UWorld* UMissionObjective::GetWorld() const
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return nullptr;
	}
	const UObject* OuterObject = GetOuter();
	return OuterObject ? OuterObject->GetWorld() : nullptr;
}

APawn* UMissionObjective::GetPlayerPawn() const
{
	const APlayerController* PC = Player.Get();
	return PC ? PC->GetPawn() : nullptr;
}
