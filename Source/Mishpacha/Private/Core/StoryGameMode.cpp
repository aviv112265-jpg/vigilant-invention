// MISHPACHA - Story mode: the three Sela siblings, switchable.
#include "Core/StoryGameMode.h"
#include "Core/MishpachaGameInstance.h"
#include "Family/FamilySubsystem.h"
#include "Family/SiblingDefinition.h"
#include "Save/MishpachaSaveGame.h"
#include "Characters/MishpachaCharacter.h"
#include "Abilities/RafiBackupAbility.h"
#include "Abilities/ShiraConnectionsAbility.h"
#include "Abilities/OmerDroneAbility.h"
#include "Mishpacha.h"
#include "Engine/World.h"

AStoryGameMode::AStoryGameMode()
{
}

void AStoryGameMode::CreateDefaultFamily()
{
	struct FDefaults
	{
		const TCHAR* Id;
		FText Name;
		FText Tagline;
		TSubclassOf<USiblingAbilityComponent> Ability;
		int32 Money;
		FLinearColor Color;
	};
	const FDefaults Family[] = {
		{ TEXT("Rafi"), NSLOCTEXT("MishpachaFamily", "RafiName", "Rafi"), NSLOCTEXT("MishpachaFamily", "RafiTag", "The big brother. Runs the business."), URafiBackupAbility::StaticClass(), 8000, FLinearColor(0.85f, 0.35f, 0.08f) },
		{ TEXT("Shira"), NSLOCTEXT("MishpachaFamily", "ShiraName", "Shira"), NSLOCTEXT("MishpachaFamily", "ShiraTag", "The lawyer. Wants out."), UShiraConnectionsAbility::StaticClass(), 12000, FLinearColor(0.05f, 0.45f, 0.45f) },
		{ TEXT("Omer"), NSLOCTEXT("MishpachaFamily", "OmerName", "Omer"), NSLOCTEXT("MishpachaFamily", "OmerTag", "The youngest. Just out of the army."), UOmerDroneAbility::StaticClass(), 1500, FLinearColor(0.35f, 0.42f, 0.12f) },
	};

	for (const FDefaults& Entry : Family)
	{
		USiblingDefinition* Definition = NewObject<USiblingDefinition>(this, FName(*FString::Printf(TEXT("DefaultSibling_%s"), Entry.Id)));
		Definition->SiblingId = FName(Entry.Id);
		Definition->DisplayName = Entry.Name;
		Definition->Tagline = Entry.Tagline;
		Definition->PawnClass = AMishpachaCharacter::StaticClass();
		Definition->AbilityClass = Entry.Ability;
		Definition->StartingMoney = Entry.Money;
		Definition->AccentColor = Entry.Color;
		FamilyDefinitions.Add(Definition);
	}
	UE_LOG(LogMishpacha, Log, TEXT("StoryGameMode: no FamilyDefinitions assigned - using the built-in Rafi / Shira / Omer."));
}

void AStoryGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The city (and its sibling spawn points) must exist before the family spawns.
	EnsureAutoContent();
	if (FamilyDefinitions.Num() == 0 && bAutoFamily)
	{
		CreateDefaultFamily();
	}

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

void AStoryGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (AutosaveInterval > 0.f)
	{
		GetWorldTimerManager().SetTimer(AutosaveTimer, this, &AStoryGameMode::SaveProgress, AutosaveInterval, true);
	}
}

void AStoryGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AutosaveTimer);
	if (EndPlayReason == EEndPlayReason::Quit)
	{
		SaveProgress(); // not on a level change: "new story" reloads the level after wiping the save
	}
	Super::EndPlay(EndPlayReason);
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
