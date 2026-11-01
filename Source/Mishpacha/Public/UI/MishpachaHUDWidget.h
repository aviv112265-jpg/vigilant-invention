// MISHPACHA - base class for the in-game HUD widget (design it in UMG: WBP_HUD).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Missions/MissionTypes.h"
#include "MishpachaHUDWidget.generated.h"

/**
 * Create a Widget Blueprint with this as parent (WBP_HUD) and implement the events.
 * Our own HUD language: heat "siren bar", sibling color accents, the family group-chat switch transition.
 * Hebrew is right-to-left: set text Justification/Flow Direction to Culture/Auto in UMG.
 */
UCLASS(Abstract)
class MISHPACHA_API UMishpachaHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnHeatChanged(int32 NewLevel, int32 OldLevel);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnMoneyChanged(int32 NewMoney);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnHealthChanged(float NewHealth, float MaxHealth);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnObjectiveChanged(const FReplicatedMissionState& MissionState);

	/** Screen just went dark: show the family group chat for the target sibling. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnSiblingSwitchStarted(int32 TargetIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnSiblingSwitched(int32 OldIndex, int32 NewIndex);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnSwitchMenuToggled(bool bOpen);

	UFUNCTION(BlueprintImplementableEvent, Category = "HUD")
	void OnNotification(const FText& Message);
};
