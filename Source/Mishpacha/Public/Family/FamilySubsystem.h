// MISHPACHA - the Sela family: three switchable siblings (Story mode).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FamilySubsystem.generated.h"

class AMishpachaCharacter;
class APlayerController;
class AController;
class USiblingDefinition;
class UMishpachaSaveGame;

UENUM(BlueprintType)
enum class ESiblingSwitchResult : uint8
{
	Started,
	NotInitialized,
	InvalidIndex,
	AlreadyActive,
	Busy,
	BlockedByHeat,
	BlockedByMission
};

USTRUCT(BlueprintType)
struct FSiblingRuntimeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Family")
	TObjectPtr<USiblingDefinition> Definition = nullptr;

	/** Live pawn, or null while the sibling is "virtualized" far away from the player. */
	UPROPERTY(BlueprintReadOnly, Category = "Family")
	TObjectPtr<AMishpachaCharacter> Pawn = nullptr;

	/** Where the sibling is while virtualized (initially their spawn point). */
	UPROPERTY(BlueprintReadOnly, Category = "Family")
	FTransform StoredTransform;

	/** Each sibling has their own wallet (NIS). The active one is mirrored in the PlayerState. */
	UPROPERTY(BlueprintReadOnly, Category = "Family")
	int32 Money = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMishpachaSiblingSwitchStarted, int32, TargetIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMishpachaSiblingSwitched, int32, OldIndex, int32, NewIndex);

/**
 * Server-side owner of the three siblings.
 *
 * Switch flow (our "family group chat" transition):
 *   fade to black -> OnSwitchStarted (UI shows the group chat) -> unpossess, old sibling gets an AI controller
 *   -> possess target -> target is "parked" until ground exists under it (World Partition streaming) or timeout
 *   -> fade in -> OnSiblingSwitched.
 *
 * Inactive siblings far from the player are virtualized (pawn destroyed, transform stored) and
 * re-spawned when the player comes near or switches to them.
 */
UCLASS()
class MISHPACHA_API UFamilySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;

	void InitializeFamily(const TArray<TObjectPtr<USiblingDefinition>>& Definitions, const UMishpachaSaveGame* Save);
	void PossessInitial(APlayerController* PC);
	void RespawnActiveSibling(AController* DeadController, const FTransform& Where);
	void NotifySiblingDied(AMishpachaCharacter* DeadCharacter);
	void WriteToSave(UMishpachaSaveGame* Save) const;

	UFUNCTION(BlueprintCallable, Category = "Family")
	ESiblingSwitchResult RequestSwitch(APlayerController* PC, int32 TargetIndex);

	UFUNCTION(BlueprintPure, Category = "Family")
	bool IsFamilyInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "Family")
	bool IsSwitching() const { return PendingIndex != INDEX_NONE; }

	UFUNCTION(BlueprintPure, Category = "Family")
	bool HasController() const { return ControllingPlayer.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Family")
	APlayerController* GetControllingPlayer() const { return ControllingPlayer.Get(); }

	UFUNCTION(BlueprintPure, Category = "Family")
	int32 GetActiveIndex() const { return ActiveIndex; }

	UFUNCTION(BlueprintPure, Category = "Family")
	int32 GetNumSiblings() const { return Siblings.Num(); }

	UFUNCTION(BlueprintPure, Category = "Family")
	FSiblingRuntimeState GetSiblingState(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "Family")
	int32 FindSiblingIndex(FName SiblingId) const;

	UFUNCTION(BlueprintPure, Category = "Family")
	FName GetActiveSiblingId() const;

	UPROPERTY(BlueprintAssignable, Category = "Family")
	FOnMishpachaSiblingSwitchStarted OnSwitchStarted;

	UPROPERTY(BlueprintAssignable, Category = "Family")
	FOnMishpachaSiblingSwitched OnSiblingSwitched;

	/** Fade duration (s) at both ends of a switch. */
	float FadeDuration = 0.5f;

	/** Extra time the screen stays dark so the group-chat UI can play. */
	float TransitionHoldTime = 0.8f;

	/** Max seconds to wait for streamed-in ground under the target sibling. */
	float GroundWaitTimeout = 10.f;

	/** Inactive siblings farther than this from the player are virtualized (cm). */
	float VirtualizeDistance = 20000.f;

	/** Virtualized siblings closer than this are spawned back in (cm). */
	float MaterializeDistance = 15000.f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UPROPERTY()
	TArray<FSiblingRuntimeState> Siblings;

	TWeakObjectPtr<APlayerController> ControllingPlayer;
	int32 ActiveIndex = INDEX_NONE;
	int32 PendingIndex = INDEX_NONE;
	int32 PreviousIndex = INDEX_NONE;
	float GroundWaitElapsed = 0.f;
	bool bInitialized = false;
	bool bInputBlocked = false;

	FTimerHandle SwitchTimer;
	FTimerHandle GroundTimer;
	FTimerHandle MaintainTimer;

	AMishpachaCharacter* SpawnSibling(int32 Index, const FTransform& Where);
	FTransform FindSpawnTransform(FName SiblingId) const;
	void ExecuteSwap();
	void StartGroundWait();
	void PollGround();
	void FinishSwitch();
	void MaintainInactiveSiblings();
	void ParkPawn(AMishpachaCharacter* PawnToPark, bool bPark) const;
	bool HasGroundBelow(const FVector& Location, const AActor* IgnoreActor) const;
	void BlockInput(APlayerController* PC, bool bBlock);
	static void DestroySiblingPawn(AMishpachaCharacter* PawnToDestroy);
};
