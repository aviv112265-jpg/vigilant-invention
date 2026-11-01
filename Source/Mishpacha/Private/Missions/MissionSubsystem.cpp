// MISHPACHA - runs the active mission (server).
#include "Missions/MissionSubsystem.h"
#include "Missions/MissionDefinition.h"
#include "Missions/MissionObjective.h"
#include "Missions/MissionTypes.h"
#include "Characters/MishpachaCharacter.h"
#include "Core/MishpachaGameInstance.h"
#include "Core/MishpachaGameState.h"
#include "Core/MishpachaPlayerController.h"
#include "Core/MishpachaPlayerState.h"
#include "Core/StoryGameMode.h"
#include "Save/MishpachaSaveGame.h"
#include "Mishpacha.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaMissions"

bool UMissionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMissionSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TickHandle);
	}
	ActiveMission = nullptr;
	ActiveObjectives.Reset();
	Super::Deinitialize();
}

UMishpachaSaveGame* UMissionSubsystem::GetSave() const
{
	UWorld* World = GetWorld();
	UMishpachaGameInstance* GI = World ? World->GetGameInstance<UMishpachaGameInstance>() : nullptr;
	return GI ? GI->GetSave() : nullptr;
}

bool UMissionSubsystem::IsSwitchingAllowed() const
{
	return !ActiveMission || ActiveMission->bAllowSwitching;
}

bool UMissionSubsystem::IsMissionCompleted(FName MissionId) const
{
	const UMishpachaSaveGame* Save = GetSave();
	return Save && Save->CompletedMissions.Contains(MissionId);
}

bool UMissionSubsystem::CanStartMission(const UMissionDefinition* Mission, const APlayerController* InPlayer) const
{
	if (!Mission || !InPlayer || IsMissionActive())
	{
		return false;
	}

	if (!Mission->bRepeatable && IsMissionCompleted(Mission->MissionId))
	{
		return false;
	}

	for (const FName& Prerequisite : Mission->Prerequisites)
	{
		if (!IsMissionCompleted(Prerequisite))
		{
			return false;
		}
	}

	if (!Mission->RequiredSibling.IsNone())
	{
		const AMishpachaCharacter* PlayerCharacter = Cast<AMishpachaCharacter>(InPlayer->GetPawn());
		if (!PlayerCharacter || PlayerCharacter->GetSiblingId() != Mission->RequiredSibling)
		{
			return false;
		}
	}
	return true;
}

bool UMissionSubsystem::StartMission(UMissionDefinition* Mission, APlayerController* InPlayer)
{
	if (!CanStartMission(Mission, InPlayer))
	{
		return false;
	}

	ActiveMission = Mission;
	MissionPlayer = InPlayer;
	ActiveObjectives.Reset();
	for (const UMissionObjective* Template : Mission->Objectives)
	{
		if (Template)
		{
			ActiveObjectives.Add(DuplicateObject<UMissionObjective>(Template, this));
		}
	}

	UE_LOG(LogMishpacha, Log, TEXT("Mission started: %s"), *Mission->MissionId.ToString());
	Notify(FText::Format(LOCTEXT("MissionStarted", "JOB: {0}"), Mission->Title));

	ObjectiveIndex = INDEX_NONE;
	GetWorld()->GetTimerManager().SetTimer(TickHandle, this, &UMissionSubsystem::TickMission, TickInterval, true);
	AdvanceObjective();
	return true;
}

void UMissionSubsystem::TickMission()
{
	if (!ActiveMission)
	{
		return;
	}
	if (!MissionPlayer.IsValid())
	{
		EndMission(false);
		return;
	}

	UMissionObjective* Objective = ActiveObjectives.IsValidIndex(ObjectiveIndex) ? ActiveObjectives[ObjectiveIndex].Get() : nullptr;
	if (!Objective)
	{
		AdvanceObjective();
		return;
	}

	Objective->TickObjective(TickInterval);
	if (Objective->HasFailed())
	{
		FailMission(LOCTEXT("ObjectiveFailed", "Objective failed."));
	}
	else if (Objective->IsComplete())
	{
		AdvanceObjective();
	}
}

void UMissionSubsystem::AdvanceObjective()
{
	if (!ActiveMission)
	{
		return;
	}

	if (ActiveObjectives.IsValidIndex(ObjectiveIndex) && ActiveObjectives[ObjectiveIndex])
	{
		ActiveObjectives[ObjectiveIndex]->Deactivate();
	}

	++ObjectiveIndex;
	if (ObjectiveIndex >= ActiveObjectives.Num())
	{
		CompleteMission();
		return;
	}

	if (UMissionObjective* Next = ActiveObjectives[ObjectiveIndex])
	{
		Next->Activate(this, MissionPlayer.Get());
	}
	PushReplicatedState();
}

void UMissionSubsystem::CompleteMission()
{
	if (!ActiveMission)
	{
		return;
	}

	const FName MissionId = ActiveMission->MissionId;
	const FText Title = ActiveMission->Title;
	const int32 Reward = ActiveMission->RewardMoney;

	if (APlayerController* PC = MissionPlayer.Get())
	{
		if (AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>())
		{
			PS->AddMoney(Reward);
		}
	}

	if (UMishpachaSaveGame* Save = GetSave())
	{
		Save->CompletedMissions.Add(MissionId);
	}

	Notify(FText::Format(LOCTEXT("MissionPassed", "JOB DONE: {0}  +NIS {1}"), Title, FText::AsNumber(Reward)));
	EndMission(true);

	if (AStoryGameMode* StoryMode = GetWorld()->GetAuthGameMode<AStoryGameMode>())
	{
		StoryMode->SaveProgress();
	}
}

void UMissionSubsystem::FailMission(FText Reason)
{
	if (!ActiveMission)
	{
		return;
	}
	Notify(FText::Format(LOCTEXT("MissionFailed", "JOB FAILED: {0}"), Reason));
	EndMission(false);
}

void UMissionSubsystem::NotifyPlayerDied(AController* DeadController)
{
	if (ActiveMission && DeadController && DeadController == MissionPlayer.Get())
	{
		FailMission(LOCTEXT("FailedDied", "You went down."));
	}
}

void UMissionSubsystem::EndMission(bool bSuccess)
{
	const FName MissionId = ActiveMission ? ActiveMission->MissionId : NAME_None;

	GetWorld()->GetTimerManager().ClearTimer(TickHandle);
	if (ActiveObjectives.IsValidIndex(ObjectiveIndex) && ActiveObjectives[ObjectiveIndex])
	{
		ActiveObjectives[ObjectiveIndex]->Deactivate();
	}

	ActiveMission = nullptr;
	ActiveObjectives.Reset();
	ObjectiveIndex = INDEX_NONE;
	MissionPlayer = nullptr;

	PushReplicatedState();
	OnMissionEnded.Broadcast(MissionId, bSuccess);
}

void UMissionSubsystem::PushReplicatedState()
{
	FReplicatedMissionState State;
	if (ActiveMission)
	{
		State.bActive = true;
		State.MissionId = ActiveMission->MissionId;
		State.Title = ActiveMission->Title;
		State.ObjectiveIndex = ObjectiveIndex;
		State.ObjectiveCount = ActiveObjectives.Num();
		if (ActiveObjectives.IsValidIndex(ObjectiveIndex) && ActiveObjectives[ObjectiveIndex])
		{
			State.ObjectiveText = ActiveObjectives[ObjectiveIndex]->Description;
		}
	}

	if (AMishpachaGameState* GameState = GetWorld()->GetGameState<AMishpachaGameState>())
	{
		GameState->SetMissionState(State);
	}
}

void UMissionSubsystem::Notify(const FText& Message) const
{
	if (AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(MissionPlayer.Get()))
	{
		PC->ClientShowNotification(Message);
	}
}

#undef LOCTEXT_NAMESPACE
