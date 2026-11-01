// MISHPACHA - replicated world state.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Missions/MissionTypes.h"
#include "MishpachaGameState.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMishpachaMissionStateChanged, const FReplicatedMissionState&, MissionState);

UCLASS()
class MISHPACHA_API AMishpachaGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Mission")
	const FReplicatedMissionState& GetMissionState() const { return MissionState; }

	/** Server only. */
	void SetMissionState(const FReplicatedMissionState& NewState);

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMishpachaMissionStateChanged OnMissionStateChanged;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_MissionState)
	FReplicatedMissionState MissionState;

	UFUNCTION()
	void OnRep_MissionState();
};
