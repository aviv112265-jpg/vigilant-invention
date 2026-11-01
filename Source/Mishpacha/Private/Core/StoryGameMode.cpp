// MISHPACHA - Story mode: the three Sela siblings, switchable.
#include "Core/StoryGameMode.h"
#include "Core/MishpachaGameInstance.h"
#include "Family/FamilySubsystem.h"
#include "Family/SiblingDefinition.h"
#include "Save/MishpachaSaveGame.h"
#include "Characters/MishpachaCharacter.h"
#include "Mishpacha.h"
#include "Engine/World.h"

AStoryGameMode::AStoryGameMode()
{
}

void AStoryGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	if (Family && FamilyDefinitions.Num() > 0)
	{
		if (!Family->IsFamilyInitialized())
		{
			UMishpachaGameInstance* GI = GetGameInstance<UMishpachaGameInstance>();
			const UMishpachaSaveGame* Save = (GI && bLoadSaveOnStart) ? GI->GetSave() : nullptr;
			Family->InitializeFamily(FamilyDefinitions, Save);
			Family->OnSiblingSwitched.AddUniqueDynamic(this, &AStoryGameMode::HandleSiblingSwitched);
		}

		if (!Family->HasController())
		{
			Family->PossessInitial(NewPlayer);
			return;
		}
		// Extra players in Story mode (future "family co-op") fall through to a normal spawn for now.
	}
	else if (FamilyDefinitions.Num() == 0)
	{
		UE_LOG(LogMishpacha, Warning, TEXT("StoryGameMode has no FamilyDefinitions - spawning the default pawn instead. Create BP_StoryGameMode and assign Rafi/Shira/Omer."));
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AStoryGameMode::OnCharacterDied(AMishpachaCharacter* DeadCharacter, AController* VictimController, AController* Killer)
{
	if (UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>())
	{
		Family->NotifySiblingDied(DeadCharacter);
	}
	Super::OnCharacterDied(DeadCharacter, VictimController, Killer);
}

void AStoryGameMode::RespawnPlayer(AController* DeadController, const FVector& DeathLocation)
{
	UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	if (Family && Family->IsFamilyInitialized() && DeadController && DeadController == Family->GetControllingPlayer())
	{
		Family->RespawnActiveSibling(DeadController, FindHospitalTransform(DeathLocation, DeadController));
		return;
	}
	Super::RespawnPlayer(DeadController, DeathLocation);
}

void AStoryGameMode::SaveProgress()
{
	UMishpachaGameInstance* GI = GetGameInstance<UMishpachaGameInstance>();
	if (!GI)
	{
		return;
	}

	if (UMishpachaSaveGame* Save = GI->GetSave())
	{
		if (UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>())
		{
			if (Family->IsFamilyInitialized())
			{
				Family->WriteToSave(Save);
			}
		}
		GI->SaveToDisk();
	}
}

void AStoryGameMode::HandleSiblingSwitched(int32 OldIndex, int32 NewIndex)
{
	if (bAutosaveOnSwitch && OldIndex != INDEX_NONE && OldIndex != NewIndex)
	{
		SaveProgress();
	}
}
