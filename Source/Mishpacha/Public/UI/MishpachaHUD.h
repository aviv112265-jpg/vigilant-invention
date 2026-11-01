// MISHPACHA - HUD actor: creates the UMG widget and feeds it game events.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Missions/MissionTypes.h"
#include "MishpachaHUD.generated.h"

class UMishpachaHUDWidget;
class AMishpachaPlayerState;
class AMishpachaCharacter;

/**
 * If HUDWidgetClass is set (BP child of this HUD), creates the widget and forwards events to it.
 * Without a widget, draws a small debug overlay so the gray-box build is playable immediately.
 */
UCLASS()
class MISHPACHA_API AMishpachaHUD : public AHUD
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "HUD")
	UMishpachaHUDWidget* GetHUDWidget() const { return HUDWidget; }

	void ShowNotification(const FText& Message);
	void SetSwitchMenuOpen(bool bOpen);

	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMishpachaHUDWidget> HUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	bool bDrawDebugOverlay = true;

	UPROPERTY()
	TObjectPtr<UMishpachaHUDWidget> HUDWidget;

	UFUNCTION()
	void HandleHeatChanged(int32 NewLevel, int32 OldLevel);

	UFUNCTION()
	void HandleMoneyChanged(int32 NewMoney);

	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float MaxHealth);

	UFUNCTION()
	void HandleMissionStateChanged(const FReplicatedMissionState& MissionState);

	UFUNCTION()
	void HandleSwitchStarted(int32 TargetIndex);

	UFUNCTION()
	void HandleSiblingSwitched(int32 OldIndex, int32 NewIndex);

private:
	TWeakObjectPtr<AMishpachaPlayerState> BoundPlayerState;
	TWeakObjectPtr<AMishpachaCharacter> BoundCharacter;
	bool bBoundGameState = false;
	bool bBoundFamily = false;
	FTimerHandle BindTimer;

	FText LastNotification;
	double LastNotificationTime = -1000.0;

	void RefreshBindings();
};
