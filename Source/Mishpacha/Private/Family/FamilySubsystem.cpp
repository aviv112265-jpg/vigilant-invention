// MISHPACHA - the Sela family: three switchable siblings (Story mode).
#include "Family/FamilySubsystem.h"
#include "Family/SiblingDefinition.h"
#include "Family/SiblingSpawnPoint.h"
#include "Characters/MishpachaCharacter.h"
#include "Core/MishpachaPlayerState.h"
#include "Heat/HeatComponent.h"
#include "Missions/MissionSubsystem.h"
#include "Save/MishpachaSaveGame.h"
#include "Mishpacha.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

bool UFamilySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UFamilySubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	Siblings.Reset();
	Super::Deinitialize();
}

void UFamilySubsystem::InitializeFamily(const TArray<TObjectPtr<USiblingDefinition>>& Definitions, const UMishpachaSaveGame* Save)
{
	Siblings.Reset();

	for (USiblingDefinition* Definition : Definitions)
	{
		if (!Definition)
		{
			UE_LOG(LogMishpacha, Warning, TEXT("FamilySubsystem: skipping empty sibling definition slot."));
			continue;
		}

		FSiblingRuntimeState State;
		State.Definition = Definition;
		State.Money = Definition->StartingMoney;
		State.StoredTransform = FindSpawnTransform(Definition->SiblingId);

		if (Save)
		{
			if (const FSiblingSaveData* Saved = Save->Siblings.Find(Definition->SiblingId))
			{
				State.Money = Saved->Money;
				if (Saved->bHasTransform)
				{
					State.StoredTransform = Saved->Transform;
				}
			}
		}
		Siblings.Add(State);
	}

	bInitialized = Siblings.Num() > 0;
	ActiveIndex = bInitialized ? FMath::Clamp(Save ? Save->ActiveSiblingIndex : 0, 0, Siblings.Num() - 1) : INDEX_NONE;

	if (bInitialized)
	{
		GetWorld()->GetTimerManager().SetTimer(MaintainTimer, this, &UFamilySubsystem::MaintainInactiveSiblings, 2.f, true);
	}
}

void UFamilySubsystem::PossessInitial(APlayerController* PC)
{
	if (!bInitialized || !PC || !Siblings.IsValidIndex(ActiveIndex))
	{
		return;
	}

	ControllingPlayer = PC;
	FSiblingRuntimeState& Active = Siblings[ActiveIndex];
	if (!IsValid(Active.Pawn))
	{
		Active.Pawn = SpawnSibling(ActiveIndex, Active.StoredTransform);
	}
	if (!Active.Pawn)
	{
		UE_LOG(LogMishpacha, Error, TEXT("FamilySubsystem: failed to spawn the starting sibling."));
		return;
	}

	if (AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>())
	{
		PS->SetMoney(Active.Money);
	}

	PC->Possess(Active.Pawn);
	// Park AFTER possessing: possession restarts the pawn and resets its movement mode.
	ParkPawn(Active.Pawn, true);

	PreviousIndex = INDEX_NONE;
	PendingIndex = ActiveIndex;
	StartGroundWait();
}

ESiblingSwitchResult UFamilySubsystem::RequestSwitch(APlayerController* PC, int32 TargetIndex)
{
	if (!bInitialized || !PC || PC != ControllingPlayer.Get())
	{
		return ESiblingSwitchResult::NotInitialized;
	}
	if (!Siblings.IsValidIndex(TargetIndex) || !Siblings[TargetIndex].Definition)
	{
		return ESiblingSwitchResult::InvalidIndex;
	}
	if (TargetIndex == ActiveIndex)
	{
		return ESiblingSwitchResult::AlreadyActive;
	}
	if (IsSwitching())
	{
		return ESiblingSwitchResult::Busy;
	}

	AMishpachaCharacter* Current = Siblings.IsValidIndex(ActiveIndex) ? Siblings[ActiveIndex].Pawn.Get() : nullptr;
	if (!Current || PC->GetPawn() != Current || Current->IsDead())
	{
		// In a vehicle, flying the drone, or dead.
		return ESiblingSwitchResult::Busy;
	}

	if (const AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>())
	{
		if (const UHeatComponent* Heat = PS->GetHeatComponent())
		{
			if (Heat->GetHeatLevel() > 0)
			{
				return ESiblingSwitchResult::BlockedByHeat;
			}
		}
	}

	if (const UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		if (!Missions->IsSwitchingAllowed())
		{
			return ESiblingSwitchResult::BlockedByMission;
		}
	}

	PreviousIndex = ActiveIndex;
	PendingIndex = TargetIndex;
	BlockInput(PC, true);

	if (PC->IsLocalController() && PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FadeDuration, FLinearColor::Black, true, true);
	}

	OnSwitchStarted.Broadcast(TargetIndex);
	GetWorld()->GetTimerManager().SetTimer(SwitchTimer, this, &UFamilySubsystem::ExecuteSwap, FadeDuration + TransitionHoldTime, false);
	return ESiblingSwitchResult::Started;
}

void UFamilySubsystem::ExecuteSwap()
{
	APlayerController* PC = ControllingPlayer.Get();
	if (!PC || !Siblings.IsValidIndex(PendingIndex) || !Siblings.IsValidIndex(ActiveIndex))
	{
		PendingIndex = INDEX_NONE;
		if (PC)
		{
			BlockInput(PC, false);
		}
		return;
	}

	FSiblingRuntimeState& Old = Siblings[ActiveIndex];
	FSiblingRuntimeState& New = Siblings[PendingIndex];
	AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>();

	if (!IsValid(New.Pawn))
	{
		New.Pawn = SpawnSibling(PendingIndex, New.StoredTransform);
	}
	if (!New.Pawn)
	{
		UE_LOG(LogMishpacha, Error, TEXT("FamilySubsystem: could not spawn sibling %d - switch cancelled."), PendingIndex);
		PendingIndex = INDEX_NONE;
		FinishSwitch();
		return;
	}

	if (PS)
	{
		Old.Money = PS->GetMoney();
	}

	AMishpachaCharacter* OldPawn = Old.Pawn;
	PC->UnPossess();
	if (OldPawn)
	{
		// The old sibling keeps "living" under AI control.
		OldPawn->SpawnDefaultController();
	}

	if (AController* AIController = New.Pawn->GetController())
	{
		AIController->UnPossess();
		AIController->Destroy();
	}

	if (PS)
	{
		PS->SetMoney(New.Money);
	}

	PC->Possess(New.Pawn);
	ParkPawn(New.Pawn, true);

	ActiveIndex = PendingIndex;
	StartGroundWait();
}

void UFamilySubsystem::StartGroundWait()
{
	GroundWaitElapsed = 0.f;
	GetWorld()->GetTimerManager().SetTimer(GroundTimer, this, &UFamilySubsystem::PollGround, 0.1f, true);
}

void UFamilySubsystem::PollGround()
{
	GroundWaitElapsed += 0.1f;

	AMishpachaCharacter* ActivePawn = Siblings.IsValidIndex(ActiveIndex) ? Siblings[ActiveIndex].Pawn.Get() : nullptr;
	const bool bGroundReady = ActivePawn && HasGroundBelow(ActivePawn->GetActorLocation(), ActivePawn);
	if (ActivePawn && !bGroundReady && GroundWaitElapsed < GroundWaitTimeout)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(GroundTimer);
	if (ActivePawn)
	{
		ParkPawn(ActivePawn, false);
	}
	FinishSwitch();
}

void UFamilySubsystem::FinishSwitch()
{
	APlayerController* PC = ControllingPlayer.Get();
	if (PC)
	{
		if (PC->IsLocalController() && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, FadeDuration, FLinearColor::Black, true, false);
		}
		BlockInput(PC, false);
	}

	const int32 FromIndex = PreviousIndex;
	PendingIndex = INDEX_NONE;
	PreviousIndex = INDEX_NONE;
	OnSiblingSwitched.Broadcast(FromIndex, ActiveIndex);
}

void UFamilySubsystem::RespawnActiveSibling(AController* DeadController, const FTransform& Where)
{
	APlayerController* PC = Cast<APlayerController>(DeadController);
	if (!PC || !Siblings.IsValidIndex(ActiveIndex))
	{
		return;
	}

	FSiblingRuntimeState& Active = Siblings[ActiveIndex];
	Active.Pawn = SpawnSibling(ActiveIndex, Where);
	if (!Active.Pawn)
	{
		return;
	}

	PC->Possess(Active.Pawn);
	ParkPawn(Active.Pawn, true);
	PreviousIndex = ActiveIndex;
	PendingIndex = ActiveIndex;
	StartGroundWait();
}

void UFamilySubsystem::NotifySiblingDied(AMishpachaCharacter* DeadCharacter)
{
	for (int32 Index = 0; Index < Siblings.Num(); ++Index)
	{
		FSiblingRuntimeState& State = Siblings[Index];
		if (State.Pawn != DeadCharacter)
		{
			continue;
		}

		State.Pawn = nullptr;
		if (Index != ActiveIndex && State.Definition)
		{
			// An inactive sibling was killed while AI-controlled: they "come home from the hospital".
			State.StoredTransform = FindSpawnTransform(State.Definition->SiblingId);
		}
		return;
	}
}

void UFamilySubsystem::WriteToSave(UMishpachaSaveGame* Save) const
{
	if (!Save)
	{
		return;
	}

	const APlayerController* PC = ControllingPlayer.Get();
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;

	for (int32 Index = 0; Index < Siblings.Num(); ++Index)
	{
		const FSiblingRuntimeState& State = Siblings[Index];
		if (!State.Definition)
		{
			continue;
		}

		FSiblingSaveData Data;
		Data.bHasTransform = true;
		Data.Transform = (IsValid(State.Pawn) && !State.Pawn->IsDead()) ? State.Pawn->GetActorTransform() : State.StoredTransform;
		Data.Transform.SetScale3D(FVector::OneVector);
		Data.Money = (Index == ActiveIndex && PS) ? PS->GetMoney() : State.Money;
		Save->Siblings.Add(State.Definition->SiblingId, Data);
	}
	Save->ActiveSiblingIndex = FMath::Max(0, ActiveIndex);
}

FSiblingRuntimeState UFamilySubsystem::GetSiblingState(int32 Index) const
{
	return Siblings.IsValidIndex(Index) ? Siblings[Index] : FSiblingRuntimeState();
}

int32 UFamilySubsystem::FindSiblingIndex(FName SiblingId) const
{
	for (int32 Index = 0; Index < Siblings.Num(); ++Index)
	{
		if (Siblings[Index].Definition && Siblings[Index].Definition->SiblingId == SiblingId)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FName UFamilySubsystem::GetActiveSiblingId() const
{
	return (Siblings.IsValidIndex(ActiveIndex) && Siblings[ActiveIndex].Definition) ? Siblings[ActiveIndex].Definition->SiblingId : NAME_None;
}

AMishpachaCharacter* UFamilySubsystem::SpawnSibling(int32 Index, const FTransform& Where)
{
	if (!Siblings.IsValidIndex(Index))
	{
		return nullptr;
	}

	const USiblingDefinition* Definition = Siblings[Index].Definition;
	UClass* PawnClass = (Definition && Definition->PawnClass) ? Definition->PawnClass.Get() : AMishpachaCharacter::StaticClass();

	FTransform SpawnTransform = Where;
	SpawnTransform.SetScale3D(FVector::OneVector);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AMishpachaCharacter* NewPawn = GetWorld()->SpawnActor<AMishpachaCharacter>(PawnClass, SpawnTransform, Params);
	if (NewPawn && Definition)
	{
		NewPawn->SetSiblingId(Definition->SiblingId);
		if (Definition->AbilityClass)
		{
			NewPawn->EnsureAbility(Definition->AbilityClass);
		}
	}
	return NewPawn;
}

FTransform UFamilySubsystem::FindSpawnTransform(FName SiblingId) const
{
	UWorld* World = GetWorld();
	for (TActorIterator<ASiblingSpawnPoint> It(World); It; ++It)
	{
		if (It->SiblingId == SiblingId)
		{
			return It->GetActorTransform();
		}
	}
	TActorIterator<APlayerStart> PlayerStartIt(World);
	if (PlayerStartIt)
	{
		return PlayerStartIt->GetActorTransform();
	}
	return FTransform(FVector(0.f, 0.f, 300.f));
}

void UFamilySubsystem::MaintainInactiveSiblings()
{
	const APlayerController* PC = ControllingPlayer.Get();
	const APawn* Reference = PC ? PC->GetPawn() : nullptr;
	if (!Reference || IsSwitching())
	{
		return;
	}

	const FVector ReferenceLocation = Reference->GetActorLocation();
	for (int32 Index = 0; Index < Siblings.Num(); ++Index)
	{
		if (Index == ActiveIndex)
		{
			continue;
		}

		FSiblingRuntimeState& State = Siblings[Index];
		if (IsValid(State.Pawn))
		{
			if (!State.Pawn->IsDead() && FVector::Dist(State.Pawn->GetActorLocation(), ReferenceLocation) > VirtualizeDistance)
			{
				State.StoredTransform = State.Pawn->GetActorTransform();
				DestroySiblingPawn(State.Pawn);
				State.Pawn = nullptr;
			}
		}
		else if (FVector::Dist(State.StoredTransform.GetLocation(), ReferenceLocation) < MaterializeDistance
			&& HasGroundBelow(State.StoredTransform.GetLocation(), nullptr))
		{
			State.Pawn = SpawnSibling(Index, State.StoredTransform);
			if (State.Pawn)
			{
				State.Pawn->SpawnDefaultController();
			}
		}
	}
}

void UFamilySubsystem::ParkPawn(AMishpachaCharacter* PawnToPark, bool bPark) const
{
	UCharacterMovementComponent* MoveComp = PawnToPark ? PawnToPark->GetCharacterMovement() : nullptr;
	if (!MoveComp)
	{
		return;
	}

	if (bPark)
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}
	else
	{
		MoveComp->SetMovementMode(MOVE_Falling);
	}
}

bool UFamilySubsystem::HasGroundBelow(const FVector& Location, const AActor* IgnoreActor) const
{
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(FamilyGroundCheck), false, IgnoreActor);
	const FVector Start = Location + FVector(0.f, 0.f, 100.f);
	const FVector End = Location - FVector(0.f, 0.f, 5000.f);
	return GetWorld()->LineTraceTestByObjectType(Start, End, ObjectParams, Params);
}

void UFamilySubsystem::BlockInput(APlayerController* PC, bool bBlock)
{
	if (!PC || bInputBlocked == bBlock)
	{
		return;
	}
	bInputBlocked = bBlock;
	PC->SetIgnoreMoveInput(bBlock);
	PC->SetIgnoreLookInput(bBlock);
}

void UFamilySubsystem::DestroySiblingPawn(AMishpachaCharacter* PawnToDestroy)
{
	if (!PawnToDestroy)
	{
		return;
	}
	if (AController* PawnController = PawnToDestroy->GetController())
	{
		if (!PawnController->IsPlayerController())
		{
			PawnController->Destroy();
		}
	}
	PawnToDestroy->Destroy();
}
