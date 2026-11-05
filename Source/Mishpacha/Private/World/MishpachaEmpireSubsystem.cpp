// MISHPACHA - Rafi's holdings.
#include "World/MishpachaEmpireSubsystem.h"
#include "World/MishpachaBusiness.h"
#include "Mishpacha.h"
#include "Characters/MishpachaCharacter.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Core/MishpachaGameInstance.h"
#include "Core/MishpachaPlayerController.h"
#include "Core/MishpachaPlayerState.h"
#include "Core/StoryGameMode.h"
#include "Family/FamilySubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Heat/HeatComponent.h"
#include "Missions/MissionSubsystem.h"
#include "Save/MishpachaSaveGame.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaEmpire"

static const FName RafiId(TEXT("Rafi"));

bool UMishpachaEmpireSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMishpachaEmpireSubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IncomeTimer);
		World->GetTimerManager().ClearTimer(AttackTimer);
		World->GetTimerManager().ClearTimer(AttackCheckTimer);
	}
	Super::Deinitialize();
}

UMishpachaSaveGame* UMishpachaEmpireSubsystem::GetSave() const
{
	UMishpachaGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance<UMishpachaGameInstance>() : nullptr;
	return GI ? GI->GetSave() : nullptr;
}

void UMishpachaEmpireSubsystem::RegisterBusiness(AMishpachaBusiness* Business)
{
	if (!Business)
	{
		return;
	}
	const bool bFirst = Businesses.Num() == 0;
	Businesses.Add(Business);
	if (const UMishpachaSaveGame* Save = GetSave())
	{
		Business->SetLost(Save->LostBusinesses.Contains(Business->GetBusinessId()));
		if (Save->OwnedBusinesses.Contains(Business->GetBusinessId()))
		{
			Business->SetState(EMishpachaBusinessState::Owned);
		}
	}
	if (bFirst)
	{
		StartTimers();
	}
}

void UMishpachaEmpireSubsystem::StartTimers()
{
	GetWorld()->GetTimerManager().SetTimer(IncomeTimer, this, &UMishpachaEmpireSubsystem::PayIncome, IncomeInterval, true);
	ScheduleAttack(FMath::FRandRange(AttackDelay.X, AttackDelay.Y));
}

void UMishpachaEmpireSubsystem::ScheduleAttack(float Delay)
{
	GetWorld()->GetTimerManager().SetTimer(AttackTimer, this, &UMishpachaEmpireSubsystem::RandomAttack, Delay, false);
}

int32 UMishpachaEmpireSubsystem::GetOwnedCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AMishpachaBusiness>& Business : Businesses)
	{
		Count += Business.IsValid() && Business->GetState() != EMishpachaBusinessState::ForSale ? 1 : 0;
	}
	return Count;
}

void UMishpachaEmpireSubsystem::Notify(const FText& Message) const
{
	const UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	if (AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(Family ? Family->GetControllingPlayer() : nullptr))
	{
		PC->ClientShowNotification(Message);
	}
}

void UMishpachaEmpireSubsystem::UpdateSave(const AMishpachaBusiness* Business)
{
	if (UMishpachaSaveGame* Save = GetSave())
	{
		const FName Id = Business->GetBusinessId();
		if (Business->GetState() == EMishpachaBusinessState::ForSale)
		{
			Save->OwnedBusinesses.Remove(Id);
		}
		else
		{
			Save->OwnedBusinesses.Add(Id);
		}
		if (Business->IsLost())
		{
			Save->LostBusinesses.Add(Id);
		}
		else
		{
			Save->LostBusinesses.Remove(Id);
		}
	}
	if (AStoryGameMode* Story = GetWorld()->GetAuthGameMode<AStoryGameMode>())
	{
		Story->SaveProgress();
	}
}

bool UMishpachaEmpireSubsystem::TryBuy(AMishpachaBusiness* Business, APlayerController* Buyer)
{
	AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(Buyer);
	const AMishpachaCharacter* Character = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	if (!Business || !Character || !PS || Business->GetState() != EMishpachaBusinessState::ForSale
		|| FVector::Dist2D(Character->GetActorLocation(), Business->GetActorLocation()) > AMishpachaBusiness::InteractRadius * 1.5f)
	{
		return false;
	}
	if (Character->GetSiblingId() != RafiId)
	{
		PC->ClientShowNotification(LOCTEXT("OnlyRafi", "Only Rafi buys businesses."));
		return false;
	}
	const int32 Cost = Business->GetCurrentPrice();
	if (!PS->TrySpendMoney(Cost))
	{
		PC->ClientShowNotification(FText::Format(LOCTEXT("NoMoney", "Not enough money: {0} costs {1} NIS."), Business->GetDisplayName(), FText::AsNumber(Cost)));
		return false;
	}
	Business->SetLost(false);
	Business->SetState(EMishpachaBusinessState::Owned);
	UE_LOG(LogMishpacha, Log, TEXT("Empire: bought %s for %d, now own %d"), *Business->GetBusinessId().ToString(), Cost, GetOwnedCount());
	PC->ClientShowNotification(FText::Format(LOCTEXT("Bought", "{0} is yours. {1} NIS every two minutes."), Business->GetDisplayName(), FText::AsNumber(Business->GetIncome())));
	UpdateSave(Business);
	return true;
}

void UMishpachaEmpireSubsystem::PayIncome()
{
	int32 Total = 0;
	for (const TWeakObjectPtr<AMishpachaBusiness>& Business : Businesses)
	{
		// A business under attack pays nothing until it's safe.
		Total += Business.IsValid() && Business->GetState() == EMishpachaBusinessState::Owned ? Business->GetIncome() : 0;
	}
	UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	if (Total > 0 && Family)
	{
		Family->AddSiblingMoney(RafiId, Total);
		UE_LOG(LogMishpacha, Log, TEXT("Empire: income %d"), Total);
		Notify(FText::Format(LOCTEXT("Income", "The business brought in {0} NIS for Rafi."), FText::AsNumber(Total)));
	}
}

void UMishpachaEmpireSubsystem::RandomAttack()
{
	// Not in the middle of a job, and not on top of a police chase.
	const UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	const UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>();
	const APlayerController* PC = Family ? Family->GetControllingPlayer() : nullptr;
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	const UHeatComponent* Heat = PS ? PS->GetHeatComponent() : nullptr;
	if ((Missions && Missions->IsMissionActive()) || (Heat && Heat->GetHeatLevel() > 0) || !StartAttack())
	{
		ScheduleAttack(60.f);
	}
}

bool UMishpachaEmpireSubsystem::StartAttack(AMishpachaBusiness* Target)
{
	if (Attacked.IsValid())
	{
		return false;
	}
	if (!Target)
	{
		TArray<AMishpachaBusiness*> Owned;
		for (const TWeakObjectPtr<AMishpachaBusiness>& Business : Businesses)
		{
			if (Business.IsValid() && Business->GetState() == EMishpachaBusinessState::Owned)
			{
				Owned.Add(Business.Get());
			}
		}
		if (Owned.Num() == 0)
		{
			return false;
		}
		Target = Owned[FMath::RandRange(0, Owned.Num() - 1)];
	}

	UWorld* World = GetWorld();
	const AGameStateBase* GameState = World->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
	Target->SetState(EMishpachaBusinessState::UnderAttack, Now + AttackDuration);
	Attacked = Target;

	// The more Rafi owns, the bigger the crew they send.
	const int32 Count = 3 + FMath::Min(GetOwnedCount() - 1, 3);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Crew.Reset();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = 2.f * PI * Index / Count;
		const FVector Spot = Target->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 400.f + FVector(0.f, 0.f, 100.f);
		if (APawn* Rival = World->SpawnActor<APawn>(AMishpachaRival::StaticClass(), Spot, FRotator(0.f, FMath::RadiansToDegrees(Angle), 0.f), Params))
		{
			Crew.Add(Rival);
		}
	}
	UE_LOG(LogMishpacha, Log, TEXT("Empire: %d rivals attack %s"), Crew.Num(), *Target->GetBusinessId().ToString());
	Notify(FText::Format(LOCTEXT("Attack", "A rival crew is taking {0}! Get there and stop them."), Target->GetDisplayName()));
	World->GetTimerManager().SetTimer(AttackCheckTimer, this, &UMishpachaEmpireSubsystem::CheckAttack, 1.f, true);
	return true;
}

void UMishpachaEmpireSubsystem::CheckAttack()
{
	AMishpachaBusiness* Target = Attacked.Get();
	if (!Target)
	{
		return;
	}
	bool bCrewAlive = false;
	for (const TWeakObjectPtr<APawn>& Member : Crew)
	{
		const AMishpachaCharacter* AsCharacter = Cast<AMishpachaCharacter>(Member.Get());
		bCrewAlive |= IsValid(Member.Get()) && !(AsCharacter && AsCharacter->IsDead());
	}
	if (!bCrewAlive)
	{
		EndAttack(true);
		return;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (GameState && GameState->GetServerWorldTimeSeconds() >= Target->GetAttackEndTime())
	{
		EndAttack(false);
	}
}

void UMishpachaEmpireSubsystem::EndAttack(bool bDefended)
{
	AMishpachaBusiness* Target = Attacked.Get();
	Attacked = nullptr;
	GetWorld()->GetTimerManager().ClearTimer(AttackCheckTimer);
	ScheduleAttack(FMath::FRandRange(AttackDelay.X, AttackDelay.Y));
	if (!Target)
	{
		return;
	}

	if (bDefended)
	{
		Target->SetState(EMishpachaBusinessState::Owned);
		if (UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>())
		{
			Family->AddSiblingMoney(RafiId, DefenseReward);
		}
		Notify(FText::Format(LOCTEXT("Defended", "{0} is safe. Rafi pays {1} NIS for that."), Target->GetDisplayName(), FText::AsNumber(DefenseReward)));
	}
	else
	{
		for (const TWeakObjectPtr<APawn>& Member : Crew)
		{
			if (APawn* Pawn = Member.Get())
			{
				Pawn->Destroy();
			}
		}
		Target->SetLost(true);
		Target->SetState(EMishpachaBusinessState::ForSale);
		Notify(FText::Format(LOCTEXT("Lost", "{0} is gone. Buying it back costs {1} NIS."), Target->GetDisplayName(), FText::AsNumber(Target->GetCurrentPrice())));
	}
	UE_LOG(LogMishpacha, Log, TEXT("Empire: attack on %s %s"), *Target->GetBusinessId().ToString(), bDefended ? TEXT("defended") : TEXT("lost"));
	Crew.Reset();
	UpdateSave(Target);
}

#undef LOCTEXT_NAMESPACE
