// MISHPACHA - player controller: Enhanced Input routing, sibling switching, notifications.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "MishpachaPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class AMishpachaCharacter;
class AMishpachaVehicle;
class ADronePawn;

/**
 * Routes input to whatever the player currently controls (a sibling on foot, a vehicle, Omer's drone).
 * If no Input assets are assigned in the Blueprint, a complete default keyboard/mouse + gamepad
 * setup is built at runtime, so the project is playable without creating any input assets.
 */
UCLASS()
class MISHPACHA_API AMishpachaPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMishpachaPlayerController();

	/** Story mode: switch to sibling 0/1/2 (Rafi/Shira/Omer by default). */
	UFUNCTION(BlueprintCallable, Category = "Family")
	void RequestSwitchSibling(int32 SiblingIndex);

	/** Shows a short message on this player's HUD (falls back to on-screen debug text). */
	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "UI")
	void ClientShowNotification(const FText& Message);

	UFUNCTION(BlueprintPure, Category = "Family")
	bool IsSwitchMenuOpen() const { return bSwitchMenuOpen; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void SetPawn(APawn* InPawn) override;

	// ----- Optional input assets (built at runtime when any is missing) -----
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> OnFootContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> VehicleContext;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> AbilityAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> SwitchMenuAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> Switch1Action;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> Switch2Action;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> Switch3Action;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> BrakeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> HandbrakeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Input|Actions")
	TObjectPtr<UInputAction> RadioAction;

	UFUNCTION(Server, Reliable)
	void ServerRequestSwitchSibling(int32 SiblingIndex);

private:
	bool bSwitchMenuOpen = false;

	bool HasInputAssets() const;
	void BuildDefaultInput();
	UInputAction* MakeAction(const TCHAR* ActionName, EInputActionValueType ActionValueType);
	void ApplyContextForPawn(APawn* InPawn);
	void ExecuteSwitch(int32 SiblingIndex);
	void SetSwitchMenuOpen(bool bOpen);

	AMishpachaCharacter* GetMishpachaCharacter() const;
	AMishpachaVehicle* GetMishpachaVehicle() const;
	ADronePawn* GetDrone() const;

	// Input handlers
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStarted();
	void HandleJumpCompleted();
	void HandleSprintStarted();
	void HandleSprintCompleted();
	void HandleInteract();
	void HandleFireStarted();
	void HandleFireCompleted();
	void HandleAimStarted();
	void HandleAimCompleted();
	void HandleAbility();
	void HandleSwitchMenuStarted();
	void HandleSwitchMenuCompleted();
	void HandleSwitch1();
	void HandleSwitch2();
	void HandleSwitch3();
	void HandleThrottle(const FInputActionValue& Value);
	void HandleThrottleReleased();
	void HandleBrake(const FInputActionValue& Value);
	void HandleBrakeReleased();
	void HandleSteer(const FInputActionValue& Value);
	void HandleSteerReleased();
	void HandleHandbrakeStarted();
	void HandleHandbrakeCompleted();
	void HandleRadio();
};
