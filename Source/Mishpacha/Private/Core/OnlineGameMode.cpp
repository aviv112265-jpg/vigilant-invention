// MISHPACHA - Online free roam.
#include "Core/OnlineGameMode.h"
#include "Core/MishpachaPlayerController.h"
#include "Core/MishpachaPlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"

#define LOCTEXT_NAMESPACE "MishpachaOnlineGameMode"

AOnlineGameMode::AOnlineGameMode()
{
}

void AOnlineGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	if (!NewPlayer)
	{
		return;
	}

	if (AMishpachaPlayerState* PS = NewPlayer->GetPlayerState<AMishpachaPlayerState>())
	{
		if (PS->GetMoney() <= 0)
		{
			PS->SetMoney(StartingMoney);
		}
	}

	if (AMishpachaPlayerController* MPC = Cast<AMishpachaPlayerController>(NewPlayer))
	{
		const int32 NumPlayers = GameState ? GameState->PlayerArray.Num() : 1;
		const int32 Capacity = GameSession ? GameSession->MaxPlayers : 0;
		MPC->ClientShowNotification(FText::Format(LOCTEXT("WelcomeOnline", "Welcome to Tel Avir. Players: {0}/{1}"),
			FText::AsNumber(NumPlayers), FText::AsNumber(Capacity)));
	}
}

AActor* AOnlineGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	EnsureAutoContent(); // the city (and its starts) must exist before the first player spawns

	TArray<APlayerStart*> CityStarts;
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FName(TEXT("Greybox"))))
		{
			CityStarts.Add(*It);
		}
	}
	if (CityStarts.Num() > 0)
	{
		return CityStarts[NextStartIndex++ % CityStarts.Num()];
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

#undef LOCTEXT_NAMESPACE
