// MISHPACHA - Online free roam: every player has their own character in Tel Avir.
#pragma once

#include "CoreMinimal.h"
#include "Core/MishpachaGameModeBase.h"
#include "OnlineGameMode.generated.h"

/**
 * Online free-roam mode. Player count comes from [/Script/Engine.GameSession] MaxPlayers (default 32)
 * and from the session settings used when hosting (UMishpachaSessionSubsystem::HostSession).
 */
UCLASS()
class MISHPACHA_API AOnlineGameMode : public AMishpachaGameModeBase
{
	GENERATED_BODY()

public:
	AOnlineGameMode();

	virtual void PostLogin(APlayerController* NewPlayer) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Online", meta = (ClampMin = "0"))
	int32 StartingMoney = 2500;
};
