// MISHPACHA - runs the active mission (server).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionSubsystem.generated.h"

class APlayerController;
class AController;
class UMissionDefinition;
class UMissionObjective;
class UMishpachaSaveGame;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMishpachaMissionEnded, FName, MissionId, bool, bSuccess);

/**
 * One active mission per world (skeleton). Progress is mirrored into AMishpachaGameState for every client's HUD.
 * Completed mission ids are stored in the story save.
 */
UCLASS()
class MISHPACHA_API UMissionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Mission")
	bool StartMission(UMissionDefinition* Mission, APlayerController* InPlayer);

	UFUNCTION(BlueprintCallable, Category = "Mission")
	void FailMission(FText Reason);

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsMissionActive() const { return ActiveMission != nullptr; }

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsSwitchingAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool IsMissionCompleted(FName MissionId) const;

	UFUNCTION(BlueprintPure, Category = "Mission")
	bool CanStartMission(const UMissionDefinition* Mission, const APlayerController* InPlayer) const;

	void NotifyPlayerDied(AController* DeadController);

	UPROPERTY(BlueprintAssignable, Category = "Mission")
	FOnMishpachaMissionEnded OnMissionEnded;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY()
	TObjectPtr<UMissionDefinition> ActiveMission;

	UPROPERTY()
	TArray<TObjectPtr<UMissionObjective>> ActiveObjectives;

	TWeakObjectPtr<APlayerController> MissionPlayer;
	int32 ObjectiveIndex = INDEX_NONE;
	FTimerHandle TickHandle;

	/** Last GPS target sent to clients (re-sent when it moves, e.g. the nearest car changes). */
	FVector LastGuideLocation = FVector::ZeroVector;
	bool bLastHasGuide = false;

	static constexpr float TickInterval = 0.2f;

	void TickMission();
	void AdvanceObjective();
	void CompleteMission();
	void EndMission(bool bSuccess);
	void PushReplicatedState();
	void Notify(const FText& Message) const;
	UMishpachaSaveGame* GetSave() const;
};
