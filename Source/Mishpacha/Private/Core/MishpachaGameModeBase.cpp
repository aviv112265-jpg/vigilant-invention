// MISHPACHA - rules shared by Story and Online modes.
#include "Core/MishpachaGameModeBase.h"
#include "Core/MishpachaPlayerController.h"
#include "Core/MishpachaPlayerState.h"
#include "Core/MishpachaGameState.h"
#include "Characters/MishpachaCharacter.h"
#include "Heat/HeatComponent.h"
#include "Missions/MissionSubsystem.h"
#include "UI/MishpachaHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaGameMode"

AMishpachaGameModeBase::AMishpachaGameModeBase()
{
	PlayerControllerClass = AMishpachaPlayerController::StaticClass();
	PlayerStateClass = AMishpachaPlayerState::StaticClass();
	GameStateClass = AMishpachaGameState::StaticClass();
	HUDClass = AMishpachaHUD::StaticClass();
	DefaultPawnClass = AMishpachaCharacter::StaticClass();
}

void AMishpachaGameModeBase::OnCharacterDied(AMishpachaCharacter* DeadCharacter, AController* VictimController, AController* Killer)
{
	if (UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>())
	{
		Missions->NotifyPlayerDied(VictimController);
	}

	APlayerController* PC = Cast<APlayerController>(VictimController);
	if (!PC || !DeadCharacter)
	{
		return;
	}

	int32 Fee = 0;
	if (AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>())
	{
		Fee = FMath::Min(HospitalFee, PS->GetMoney());
		PS->AddMoney(-Fee);
		if (UHeatComponent* Heat = PS->GetHeatComponent())
		{
			Heat->ClearHeat();
		}
	}

	if (AMishpachaPlayerController* MPC = Cast<AMishpachaPlayerController>(PC))
	{
		MPC->ClientShowNotification(FText::Format(LOCTEXT("HospitalBill", "You were rushed to the hospital. Bill: NIS {0}"), FText::AsNumber(Fee)));
	}

	const FVector DeathLocation = DeadCharacter->GetActorLocation();
	TWeakObjectPtr<AController> WeakController = PC;
	FTimerHandle RespawnHandle;
	GetWorldTimerManager().SetTimer(RespawnHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakController, DeathLocation]()
	{
		if (AController* ControllerToRespawn = WeakController.Get())
		{
			RespawnPlayer(ControllerToRespawn, DeathLocation);
		}
	}), FMath::Max(RespawnDelay, 0.1f), false);
}

void AMishpachaGameModeBase::RespawnPlayer(AController* DeadController, const FVector& DeathLocation)
{
	if (!DeadController)
	{
		return;
	}
	RestartPlayerAtTransform(DeadController, FindHospitalTransform(DeathLocation, DeadController));
}

FTransform AMishpachaGameModeBase::FindHospitalTransform(const FVector& NearLocation, AController* ForController)
{
	TArray<AActor*> Hospitals;
	UGameplayStatics::GetAllActorsWithTag(this, HospitalTag, Hospitals);

	AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (AActor* Hospital : Hospitals)
	{
		const double DistSq = FVector::DistSquared(Hospital->GetActorLocation(), NearLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Hospital;
		}
	}

	FTransform Result(NearLocation + FVector(0.f, 0.f, 200.f));
	if (Best)
	{
		Result = Best->GetActorTransform();
	}
	else if (AActor* PlayerStartActor = FindPlayerStart(ForController))
	{
		Result = PlayerStartActor->GetActorTransform();
	}
	Result.SetScale3D(FVector::OneVector);
	return Result;
}

#undef LOCTEXT_NAMESPACE
