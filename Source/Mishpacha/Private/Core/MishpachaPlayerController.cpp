// MISHPACHA - player controller.
#include "Core/MishpachaPlayerController.h"
#include "Characters/MishpachaCharacter.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Abilities/DronePawn.h"
#include "Family/FamilySubsystem.h"
#include "UI/MishpachaHUD.h"
#include "Core/MishpachaGameInstance.h"
#include "Core/MishpachaPlayerState.h"
#include "Heat/HeatComponent.h"
#include "Missions/MissionDefinition.h"
#include "Missions/MissionStartMarker.h"
#include "Missions/MissionSubsystem.h"
#include "Save/MishpachaSaveGame.h"
#include "World/MishpachaDayNight.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Characters/MishpachaCat.h"
#include "World/MishpachaStreetProp.h"
#include "Mishpacha.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "InputModifiers.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "MishpachaPlayerController"

namespace MishpachaInput
{
	enum EMapFlags : uint8
	{
		MF_None = 0,
		MF_Negate = 1 << 0,
		MF_Swizzle = 1 << 1,
		MF_NegateY = 1 << 2,
		MF_DeadZone = 1 << 3,
	};

	static void Map(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key, uint8 Flags = MF_None)
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (Flags & MF_DeadZone)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierDeadZone>(Context));
		}
		if (Flags & MF_Swizzle)
		{
			// (1,0) -> (0,1): turns a key press into "forward" on the Y axis.
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (Flags & MF_Negate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
		if (Flags & MF_NegateY)
		{
			UInputModifierNegate* NegateYOnly = NewObject<UInputModifierNegate>(Context);
			NegateYOnly->bX = false;
			NegateYOnly->bY = true;
			NegateYOnly->bZ = false;
			Mapping.Modifiers.Add(NegateYOnly);
		}
	}
}

AMishpachaPlayerController::AMishpachaPlayerController()
{
}

void AMishpachaPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		ApplyContextForPawn(GetPawn());
	}
}

bool AMishpachaPlayerController::HasInputAssets() const
{
	return OnFootContext && VehicleContext && MoveAction && LookAction && JumpAction && SprintAction && InteractAction
		&& FireAction && AimAction && AbilityAction && SwitchMenuAction && Switch1Action && Switch2Action && Switch3Action
		&& ThrottleAction && BrakeAction && SteerAction && HandbrakeAction && RadioAction;
}

UInputAction* AMishpachaPlayerController::MakeAction(const TCHAR* ActionName, EInputActionValueType ActionValueType)
{
	UInputAction* Action = NewObject<UInputAction>(this, FName(ActionName));
	Action->ValueType = ActionValueType;
	return Action;
}

void AMishpachaPlayerController::BuildDefaultInput()
{
	using namespace MishpachaInput;

	MoveAction = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	SprintAction = MakeAction(TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(TEXT("IA_Interact"), EInputActionValueType::Boolean);
	FireAction = MakeAction(TEXT("IA_Fire"), EInputActionValueType::Boolean);
	AimAction = MakeAction(TEXT("IA_Aim"), EInputActionValueType::Boolean);
	AbilityAction = MakeAction(TEXT("IA_Ability"), EInputActionValueType::Boolean);
	SwitchMenuAction = MakeAction(TEXT("IA_SwitchMenu"), EInputActionValueType::Boolean);
	Switch1Action = MakeAction(TEXT("IA_Switch1"), EInputActionValueType::Boolean);
	Switch2Action = MakeAction(TEXT("IA_Switch2"), EInputActionValueType::Boolean);
	Switch3Action = MakeAction(TEXT("IA_Switch3"), EInputActionValueType::Boolean);
	ThrottleAction = MakeAction(TEXT("IA_Throttle"), EInputActionValueType::Axis1D);
	BrakeAction = MakeAction(TEXT("IA_Brake"), EInputActionValueType::Axis1D);
	SteerAction = MakeAction(TEXT("IA_Steer"), EInputActionValueType::Axis1D);
	HandbrakeAction = MakeAction(TEXT("IA_Handbrake"), EInputActionValueType::Boolean);
	RadioAction = MakeAction(TEXT("IA_Radio"), EInputActionValueType::Boolean);

	// ----- On foot (also used while flying Omer's drone) -----
	OnFootContext = NewObject<UInputMappingContext>(this, TEXT("IMC_OnFoot_Default"));
	Map(OnFootContext, MoveAction, EKeys::W, MF_Swizzle);
	Map(OnFootContext, MoveAction, EKeys::S, MF_Swizzle | MF_Negate);
	Map(OnFootContext, MoveAction, EKeys::A, MF_Negate);
	Map(OnFootContext, MoveAction, EKeys::D);
	Map(OnFootContext, MoveAction, EKeys::Gamepad_Left2D, MF_DeadZone);
	Map(OnFootContext, LookAction, EKeys::Mouse2D, MF_NegateY);
	Map(OnFootContext, LookAction, EKeys::Gamepad_Right2D, MF_DeadZone | MF_NegateY);
	Map(OnFootContext, JumpAction, EKeys::SpaceBar);
	Map(OnFootContext, JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	Map(OnFootContext, SprintAction, EKeys::LeftShift);
	Map(OnFootContext, SprintAction, EKeys::Gamepad_LeftThumbstick);
	Map(OnFootContext, InteractAction, EKeys::F);
	Map(OnFootContext, InteractAction, EKeys::Gamepad_FaceButton_Top);
	Map(OnFootContext, FireAction, EKeys::LeftMouseButton);
	Map(OnFootContext, FireAction, EKeys::Gamepad_RightTriggerAxis);
	Map(OnFootContext, AimAction, EKeys::RightMouseButton);
	Map(OnFootContext, AimAction, EKeys::Gamepad_LeftTriggerAxis);
	Map(OnFootContext, AbilityAction, EKeys::Q);
	Map(OnFootContext, AbilityAction, EKeys::Gamepad_RightShoulder);
	Map(OnFootContext, SwitchMenuAction, EKeys::LeftAlt);
	Map(OnFootContext, SwitchMenuAction, EKeys::Gamepad_DPad_Down);
	Map(OnFootContext, Switch1Action, EKeys::One);
	Map(OnFootContext, Switch2Action, EKeys::Two);
	Map(OnFootContext, Switch3Action, EKeys::Three);

	// ----- Vehicle -----
	VehicleContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Vehicle_Default"));
	Map(VehicleContext, ThrottleAction, EKeys::W);
	Map(VehicleContext, ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	Map(VehicleContext, BrakeAction, EKeys::S);
	Map(VehicleContext, BrakeAction, EKeys::Gamepad_LeftTriggerAxis);
	Map(VehicleContext, SteerAction, EKeys::D);
	Map(VehicleContext, SteerAction, EKeys::A, MF_Negate);
	Map(VehicleContext, SteerAction, EKeys::Gamepad_LeftX, MF_DeadZone);
	Map(VehicleContext, HandbrakeAction, EKeys::SpaceBar);
	Map(VehicleContext, HandbrakeAction, EKeys::Gamepad_FaceButton_Right);
	Map(VehicleContext, LookAction, EKeys::Mouse2D, MF_NegateY);
	Map(VehicleContext, LookAction, EKeys::Gamepad_Right2D, MF_DeadZone | MF_NegateY);
	Map(VehicleContext, InteractAction, EKeys::F);
	Map(VehicleContext, InteractAction, EKeys::Gamepad_FaceButton_Top);
	Map(VehicleContext, RadioAction, EKeys::R);
	Map(VehicleContext, RadioAction, EKeys::Gamepad_DPad_Right);
}

void AMishpachaPlayerController::BuildMapInput()
{
	using namespace MishpachaInput;

	MapToggleAction = MakeAction(TEXT("IA_MapToggle"), EInputActionValueType::Boolean);
	MapSelectAction = MakeAction(TEXT("IA_MapSelect"), EInputActionValueType::Boolean);
	MapClearAction = MakeAction(TEXT("IA_MapClear"), EInputActionValueType::Boolean);
	MapPanAction = MakeAction(TEXT("IA_MapPan"), EInputActionValueType::Axis2D);
	MapZoomAction = MakeAction(TEXT("IA_MapZoom"), EInputActionValueType::Axis1D);
	WheelAction = MakeAction(TEXT("IA_WeaponWheel"), EInputActionValueType::Boolean);
	WheelAction->bTriggerWhenPaused = true;
	LightsAction = MakeAction(TEXT("IA_Lights"), EInputActionValueType::Boolean);

	// Always on top of the pawn context: open / close the map.
	UIContext = NewObject<UInputMappingContext>(this, TEXT("IMC_UI_Default"));
	Map(UIContext, LightsAction, EKeys::H);
	Map(UIContext, LightsAction, EKeys::Gamepad_DPad_Left);
	Map(UIContext, WheelAction, EKeys::Tab);
	Map(UIContext, WheelAction, EKeys::Gamepad_LeftShoulder);
	Map(UIContext, MapToggleAction, EKeys::M);
	Map(UIContext, MapToggleAction, EKeys::Gamepad_Special_Left);

	// Only while the map is open (highest priority, so it swallows these keys).
	MapContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Map_Default"));
	Map(MapContext, MapSelectAction, EKeys::LeftMouseButton);
	Map(MapContext, MapSelectAction, EKeys::Gamepad_FaceButton_Bottom);
	Map(MapContext, MapClearAction, EKeys::RightMouseButton);
	Map(MapContext, MapClearAction, EKeys::Gamepad_FaceButton_Left);
	Map(MapContext, MapPanAction, EKeys::W, MF_Swizzle);
	Map(MapContext, MapPanAction, EKeys::S, MF_Swizzle | MF_Negate);
	Map(MapContext, MapPanAction, EKeys::A, MF_Negate);
	Map(MapContext, MapPanAction, EKeys::D);
	Map(MapContext, MapPanAction, EKeys::Gamepad_Left2D, MF_DeadZone);
	Map(MapContext, MapZoomAction, EKeys::MouseWheelAxis);
	Map(MapContext, MapZoomAction, EKeys::Gamepad_RightTriggerAxis);
	Map(MapContext, MapZoomAction, EKeys::Gamepad_LeftTriggerAxis, MF_Negate);

	// Menus (work while the game is paused). P / Esc / Start open the pause menu and go back.
	auto MenuAction = [this](const TCHAR* ActionName)
	{
		UInputAction* Action = MakeAction(ActionName, EInputActionValueType::Boolean);
		Action->bTriggerWhenPaused = true;
		return Action;
	};
	MenuToggleAction = MenuAction(TEXT("IA_MenuToggle"));
	MenuSelectAction = MenuAction(TEXT("IA_MenuSelect"));
	MenuClickAction = MenuAction(TEXT("IA_MenuClick"));
	MenuUpAction = MenuAction(TEXT("IA_MenuUp"));
	MenuDownAction = MenuAction(TEXT("IA_MenuDown"));
	Map(UIContext, MenuToggleAction, EKeys::P);
	Map(UIContext, MenuToggleAction, EKeys::Escape);
	Map(UIContext, MenuToggleAction, EKeys::Gamepad_Special_Right);

	MenuContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Menu_Default"));
	Map(MenuContext, MenuClickAction, EKeys::LeftMouseButton);
	Map(MenuContext, MenuSelectAction, EKeys::Enter);
	Map(MenuContext, MenuSelectAction, EKeys::SpaceBar);
	Map(MenuContext, MenuSelectAction, EKeys::Gamepad_FaceButton_Bottom);
	Map(MenuContext, MenuToggleAction, EKeys::Gamepad_FaceButton_Right);
	Map(MenuContext, MenuToggleAction, EKeys::BackSpace);
	Map(MenuContext, MenuUpAction, EKeys::W);
	Map(MenuContext, MenuUpAction, EKeys::Up);
	Map(MenuContext, MenuUpAction, EKeys::Gamepad_DPad_Up);
	Map(MenuContext, MenuDownAction, EKeys::S);
	Map(MenuContext, MenuDownAction, EKeys::Down);
	Map(MenuContext, MenuDownAction, EKeys::Gamepad_DPad_Down);
}

void AMishpachaPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!HasInputAssets())
	{
		UE_LOG(LogMishpacha, Log, TEXT("Input assets not fully assigned - building default bindings at runtime."));
		BuildDefaultInput();
	}
	if (!MapToggleAction)
	{
		BuildMapInput();
	}

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogMishpacha, Error, TEXT("InputComponent is not an EnhancedInputComponent. Check Config/DefaultInput.ini."));
		return;
	}

	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::HandleMove);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::HandleLook);
	EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::HandleJumpStarted);
	EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ThisClass::HandleJumpCompleted);
	EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &ThisClass::HandleSprintStarted);
	EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &ThisClass::HandleSprintCompleted);
	EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &ThisClass::HandleInteract);
	EIC->BindAction(FireAction, ETriggerEvent::Started, this, &ThisClass::HandleFireStarted);
	EIC->BindAction(FireAction, ETriggerEvent::Completed, this, &ThisClass::HandleFireCompleted);
	EIC->BindAction(AimAction, ETriggerEvent::Started, this, &ThisClass::HandleAimStarted);
	EIC->BindAction(AimAction, ETriggerEvent::Completed, this, &ThisClass::HandleAimCompleted);
	EIC->BindAction(AbilityAction, ETriggerEvent::Started, this, &ThisClass::HandleAbility);
	EIC->BindAction(SwitchMenuAction, ETriggerEvent::Started, this, &ThisClass::HandleSwitchMenuStarted);
	EIC->BindAction(SwitchMenuAction, ETriggerEvent::Completed, this, &ThisClass::HandleSwitchMenuCompleted);
	EIC->BindAction(Switch1Action, ETriggerEvent::Started, this, &ThisClass::HandleSwitch1);
	EIC->BindAction(Switch2Action, ETriggerEvent::Started, this, &ThisClass::HandleSwitch2);
	EIC->BindAction(Switch3Action, ETriggerEvent::Started, this, &ThisClass::HandleSwitch3);
	EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ThisClass::HandleThrottle);
	EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ThisClass::HandleThrottleReleased);
	EIC->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &ThisClass::HandleBrake);
	EIC->BindAction(BrakeAction, ETriggerEvent::Completed, this, &ThisClass::HandleBrakeReleased);
	EIC->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ThisClass::HandleSteer);
	EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &ThisClass::HandleSteerReleased);
	EIC->BindAction(HandbrakeAction, ETriggerEvent::Started, this, &ThisClass::HandleHandbrakeStarted);
	EIC->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &ThisClass::HandleHandbrakeCompleted);
	EIC->BindAction(RadioAction, ETriggerEvent::Started, this, &ThisClass::HandleRadio);
	EIC->BindAction(MapToggleAction, ETriggerEvent::Started, this, &ThisClass::HandleMapToggle);
	EIC->BindAction(MapSelectAction, ETriggerEvent::Started, this, &ThisClass::HandleMapSelect);
	EIC->BindAction(MapClearAction, ETriggerEvent::Started, this, &ThisClass::HandleMapClear);
	EIC->BindAction(MapPanAction, ETriggerEvent::Triggered, this, &ThisClass::HandleMapPan);
	EIC->BindAction(MapZoomAction, ETriggerEvent::Triggered, this, &ThisClass::HandleMapZoom);
	EIC->BindAction(WheelAction, ETriggerEvent::Started, this, &ThisClass::HandleWheelStarted);
	EIC->BindAction(WheelAction, ETriggerEvent::Completed, this, &ThisClass::HandleWheelCompleted);
	EIC->BindAction(LightsAction, ETriggerEvent::Started, this, &ThisClass::HandleLights);
	EIC->BindAction(MenuToggleAction, ETriggerEvent::Started, this, &ThisClass::HandleMenuToggle);
	EIC->BindAction(MenuSelectAction, ETriggerEvent::Started, this, &ThisClass::HandleMenuSelect);
	EIC->BindAction(MenuClickAction, ETriggerEvent::Started, this, &ThisClass::HandleMenuClick);
	EIC->BindAction(MenuUpAction, ETriggerEvent::Started, this, &ThisClass::HandleMenuUp);
	EIC->BindAction(MenuDownAction, ETriggerEvent::Started, this, &ThisClass::HandleMenuDown);

	ApplyContextForPawn(GetPawn());
}

void AMishpachaPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);
	ApplyContextForPawn(InPawn);
	SetSwitchMenuOpen(false);
}

void AMishpachaPlayerController::ApplyContextForPawn(APawn* InPawn)
{
	if (!IsLocalController())
	{
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!InputSubsystem)
	{
		return;
	}

	InputSubsystem->ClearAllMappings();
	UInputMappingContext* Context = Cast<AMishpachaVehicle>(InPawn) ? VehicleContext.Get() : OnFootContext.Get();
	if (Context)
	{
		InputSubsystem->AddMappingContext(Context, 0);
	}
	if (UIContext)
	{
		InputSubsystem->AddMappingContext(UIContext, 50);
	}
	if (bMapOpen && MapContext && !bMenuOpen)
	{
		InputSubsystem->AddMappingContext(MapContext, 100);
	}
	if (bMenuOpen && MenuContext)
	{
		InputSubsystem->AddMappingContext(MenuContext, 200);
	}
}

void AMishpachaPlayerController::HandleWheelStarted()
{
	AMishpachaCharacter* MyCharacter = GetMishpachaCharacter();
	AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>();
	if (bMapOpen || bMenuOpen || bWheelOpen || !MyCharacter || MyCharacter->IsDead() || !MishpachaHUD)
	{
		return;
	}
	bWheelOpen = true;
	MyCharacter->StopFire();
	MyCharacter->SetAiming(false);
	MishpachaHUD->OpenWeaponWheel();
	ApplyCursorMode();

	// Cursor to the center: its direction from there picks the weapon.
	int32 SizeX = 0;
	int32 SizeY = 0;
	GetViewportSize(SizeX, SizeY);
	SetMouseLocation(SizeX / 2, SizeY / 2);

	// Slow motion while choosing (single player only).
	if (GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 0.25f);
	}
}

void AMishpachaPlayerController::HandleWheelCompleted()
{
	if (!bWheelOpen)
	{
		return;
	}
	bWheelOpen = false;
	ApplyCursorMode();
	if (GetNetMode() == NM_Standalone)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	}
	AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>();
	const int32 Slot = MishpachaHUD ? MishpachaHUD->CloseWeaponWheel() : INDEX_NONE;
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->RequestEquipSlot(Slot);
	}
}

void AMishpachaPlayerController::ApplyCursorMode()
{
	const bool bCursor = bMapOpen || bMenuOpen || bWheelOpen;
	bShowMouseCursor = bCursor;
	if (bCursor)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
		SetInputMode(InputMode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}

void AMishpachaPlayerController::SetMenuOpen(bool bOpen, EMishpachaMenuPage Page)
{
	if (!IsLocalController())
	{
		return;
	}
	if (bOpen && bMapOpen)
	{
		SetMapOpen(false);
	}
	if (bOpen && !bMenuOpen)
	{
		// Let go of everything that was held.
		if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
		{
			MyCharacter->StopFire();
			MyCharacter->SetAiming(false);
			MyCharacter->SetSprinting(false);
		}
		else if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
		{
			Vehicle->SetThrottle(0.f);
			Vehicle->SetBrake(0.f);
			Vehicle->SetSteering(0.f);
		}
		SetSwitchMenuOpen(false);
	}
	bMenuOpen = bOpen;

	// Pause single-player games behind the menu (online games keep running).
	if (GetNetMode() == NM_Standalone)
	{
		SetPause(bOpen);
	}
	ApplyCursorMode();
	ApplyContextForPawn(GetPawn());

	if (AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>())
	{
		MishpachaHUD->SetMenuPage(bOpen ? Page : EMishpachaMenuPage::None);
	}
}

void AMishpachaPlayerController::SetMapOpen(bool bOpen)
{
	if (bMapOpen == bOpen || !IsLocalController())
	{
		return;
	}
	bMapOpen = bOpen;

	// Let go of everything that was held when the map opened.
	if (bOpen)
	{
		if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
		{
			MyCharacter->StopFire();
			MyCharacter->SetAiming(false);
			MyCharacter->SetSprinting(false);
		}
		else if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
		{
			Vehicle->SetThrottle(0.f);
			Vehicle->SetBrake(0.f);
			Vehicle->SetSteering(0.f);
		}
		SetSwitchMenuOpen(false);
	}

	ApplyCursorMode();
	ApplyContextForPawn(GetPawn());

	if (AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>())
	{
		MishpachaHUD->SetMapOpen(bOpen);
	}
}

AMishpachaCharacter* AMishpachaPlayerController::GetMishpachaCharacter() const
{
	return Cast<AMishpachaCharacter>(GetPawn());
}

AMishpachaVehicle* AMishpachaPlayerController::GetMishpachaVehicle() const
{
	return Cast<AMishpachaVehicle>(GetPawn());
}

ADronePawn* AMishpachaPlayerController::GetDrone() const
{
	return Cast<ADronePawn>(GetPawn());
}

// ---------------------------------------------------------------------------
// Switching
// ---------------------------------------------------------------------------

void AMishpachaPlayerController::RequestSwitchSibling(int32 SiblingIndex)
{
	SetSwitchMenuOpen(false);
	if (HasAuthority())
	{
		ExecuteSwitch(SiblingIndex);
	}
	else
	{
		ServerRequestSwitchSibling(SiblingIndex);
	}
}

void AMishpachaPlayerController::ServerRequestSwitchSibling_Implementation(int32 SiblingIndex)
{
	ExecuteSwitch(SiblingIndex);
}

void AMishpachaPlayerController::ExecuteSwitch(int32 SiblingIndex)
{
	UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
	if (!Family || !Family->IsFamilyInitialized())
	{
		ClientShowNotification(LOCTEXT("SwitchStoryOnly", "Character switching is only available in Story mode."));
		return;
	}

	switch (Family->RequestSwitch(this, SiblingIndex))
	{
	case ESiblingSwitchResult::BlockedByHeat:
		ClientShowNotification(LOCTEXT("SwitchHeat", "Lose the heat before switching."));
		break;
	case ESiblingSwitchResult::BlockedByMission:
		ClientShowNotification(LOCTEXT("SwitchMission", "You can't switch during this job."));
		break;
	case ESiblingSwitchResult::Busy:
		ClientShowNotification(LOCTEXT("SwitchBusy", "Can't switch right now (get out of the vehicle / drone first)."));
		break;
	default:
		break;
	}
}

void AMishpachaPlayerController::SetSwitchMenuOpen(bool bOpen)
{
	if (bSwitchMenuOpen == bOpen)
	{
		return;
	}
	bSwitchMenuOpen = bOpen;
	if (AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>())
	{
		MishpachaHUD->SetSwitchMenuOpen(bOpen);
	}
}

void AMishpachaPlayerController::ClientShowNotification_Implementation(const FText& Message)
{
	if (AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>())
	{
		MishpachaHUD->ShowNotification(Message);
	}
	UE_LOG(LogMishpacha, Log, TEXT("[Notification] %s"), *Message.ToString());
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

void AMishpachaPlayerController::HandleMove(const FInputActionValue& Value)
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->Move(Axis);
	}
	else if (ADronePawn* Drone = GetDrone())
	{
		Drone->MoveInput(Axis);
	}
}

void AMishpachaPlayerController::HandleLook(const FInputActionValue& Value)
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	const FVector2D Axis = Value.Get<FVector2D>();
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->Look(Axis);
	}
	else if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->Look(Axis);
	}
	else if (ADronePawn* Drone = GetDrone())
	{
		Drone->LookInput(Axis);
	}
}

void AMishpachaPlayerController::HandleJumpStarted()
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->Jump();
	}
	else if (ADronePawn* Drone = GetDrone())
	{
		Drone->SetVerticalInput(1.f);
	}
}

void AMishpachaPlayerController::HandleJumpCompleted()
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->StopJumping();
	}
	else if (ADronePawn* Drone = GetDrone())
	{
		Drone->SetVerticalInput(0.f);
	}
}

void AMishpachaPlayerController::HandleSprintStarted()
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->SetSprinting(true);
	}
	else if (ADronePawn* Drone = GetDrone())
	{
		Drone->SetVerticalInput(-1.f);
	}
}

void AMishpachaPlayerController::HandleSprintCompleted()
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->SetSprinting(false);
	}
	else if (ADronePawn* Drone = GetDrone())
	{
		Drone->SetVerticalInput(0.f);
	}
}

void AMishpachaPlayerController::HandleInteract()
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->RequestExit();
	}
	else if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->Interact();
	}
}

void AMishpachaPlayerController::HandleFireStarted()
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->StartFire();
	}
}

void AMishpachaPlayerController::HandleFireCompleted()
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->StopFire();
	}
}

void AMishpachaPlayerController::HandleAimStarted()
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->SetAiming(true);
	}
}

void AMishpachaPlayerController::HandleAimCompleted()
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->SetAiming(false);
	}
}

void AMishpachaPlayerController::HandleAbility()
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (ADronePawn* Drone = GetDrone())
	{
		Drone->RequestRecall();
	}
	else if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->UseAbility();
	}
}

void AMishpachaPlayerController::HandleSwitchMenuStarted()
{
	SetSwitchMenuOpen(true);
}

void AMishpachaPlayerController::HandleSwitchMenuCompleted()
{
	SetSwitchMenuOpen(false);
}

void AMishpachaPlayerController::HandleSwitch1()
{
	RequestSwitchSibling(0);
}

void AMishpachaPlayerController::HandleSwitch2()
{
	RequestSwitchSibling(1);
}

void AMishpachaPlayerController::HandleSwitch3()
{
	RequestSwitchSibling(2);
}

void AMishpachaPlayerController::HandleThrottle(const FInputActionValue& Value)
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetThrottle(Value.Get<float>());
	}
}

void AMishpachaPlayerController::HandleThrottleReleased()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetThrottle(0.f);
	}
}

void AMishpachaPlayerController::HandleBrake(const FInputActionValue& Value)
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetBrake(Value.Get<float>());
	}
}

void AMishpachaPlayerController::HandleBrakeReleased()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetBrake(0.f);
	}
}

void AMishpachaPlayerController::HandleSteer(const FInputActionValue& Value)
{
	if (bMapOpen || bMenuOpen || bWheelOpen)
	{
		return;
	}
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetSteering(Value.Get<float>());
	}
}

void AMishpachaPlayerController::HandleSteerReleased()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetSteering(0.f);
	}
}

void AMishpachaPlayerController::HandleHandbrakeStarted()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetHandbrake(true);
	}
}

void AMishpachaPlayerController::HandleHandbrakeCompleted()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetHandbrake(false);
	}
}

void AMishpachaPlayerController::HandleLights()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->ToggleLights();
	}
}

void AMishpachaPlayerController::HandleRadio()
{
	if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->NextRadioStation();
	}
}

// ---------------------------------------------------------------------------
// Dev console commands
// ---------------------------------------------------------------------------

void AMishpachaPlayerController::Mish_StartJob(FName MissionId)
{
	UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>();
	for (TActorIterator<AMissionStartMarker> It(GetWorld()); It && Missions && HasAuthority(); ++It)
	{
		if (UMissionDefinition* Mission = It->GetMission(); Mission && Mission->MissionId == MissionId)
		{
			if (!Missions->StartMission(Mission, this))
			{
				ClientShowNotification(FText::Format(LOCTEXT("DevCantStart", "Can't start {0} (wrong sibling, prerequisites, or a job is running)."), FText::FromName(MissionId)));
			}
			return;
		}
	}
	ClientShowNotification(FText::Format(LOCTEXT("DevNoJob", "No job called {0}."), FText::FromName(MissionId)));
}

void AMishpachaPlayerController::Mish_CompleteJob(FName MissionId)
{
	UMishpachaGameInstance* GI = GetGameInstance<UMishpachaGameInstance>();
	UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (!MissionId.IsNone() && HasAuthority() && GI && GI->GetSave() && Missions)
	{
		GI->GetSave()->CompletedMissions.Add(MissionId);
		Missions->OnMissionEnded.Broadcast(MissionId, true); // markers refresh (next job in the chain appears)
		ClientShowNotification(FText::Format(LOCTEXT("DevCompleted", "Marked {0} as done."), FText::FromName(MissionId)));
	}
}

void AMishpachaPlayerController::Mish_Heat(int32 Level)
{
	AMishpachaPlayerState* PS = GetPlayerState<AMishpachaPlayerState>();
	UHeatComponent* Heat = PS ? PS->GetHeatComponent() : nullptr;
	if (HasAuthority() && Heat && GetPawn())
	{
		Heat->DebugSetHeatLevel(Level, GetPawn()->GetActorLocation());
	}
}

void AMishpachaPlayerController::Mish_EnterNearestCar()
{
	AMishpachaCharacter* MyCharacter = GetMishpachaCharacter();
	if (!HasAuthority() || !MyCharacter)
	{
		return;
	}
	AMishpachaVehicle* Nearest = nullptr;
	for (TActorIterator<AMishpachaVehicle> It(GetWorld()); It; ++It)
	{
		if (!It->HasDriver() && !It->ActorHasTag(FName(TEXT("Police")))
			&& (!Nearest || FVector::DistSquared(It->GetActorLocation(), MyCharacter->GetActorLocation()) < FVector::DistSquared(Nearest->GetActorLocation(), MyCharacter->GetActorLocation())))
		{
			Nearest = *It;
		}
	}
	if (Nearest)
	{
		Nearest->EnterVehicle(MyCharacter);
	}
}

void AMishpachaPlayerController::Mish_Equip(int32 Slot)
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->RequestEquipSlot(Slot);
	}
}

void AMishpachaPlayerController::Mish_TestRunOver()
{
	// A pedestrian, a cat and a traffic light in a row in front of the car (then Mish_AutoDrive 1).
	AMishpachaVehicle* Vehicle = GetMishpachaVehicle();
	if (!Vehicle || !HasAuthority())
	{
		return;
	}
	const FVector Forward = Vehicle->GetActorForwardVector();
	const FVector Base = Vehicle->GetActorLocation();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// No brains: they stand still in the road.
	auto StandStill = [](APawn* Spawned)
	{
		if (AController* Brain = Spawned ? Spawned->GetController() : nullptr)
		{
			Brain->UnPossess();
			Brain->Destroy();
		}
	};
	StandStill(GetWorld()->SpawnActor<APawn>(AMishpachaCivilian::StaticClass(), Base + Forward * 1500.f + FVector(0.f, 0.f, 100.f), FRotator::ZeroRotator, Params));
	StandStill(GetWorld()->SpawnActor<APawn>(AMishpachaCat::StaticClass(), Base + Forward * 2200.f + FVector(0.f, 0.f, 30.f), FRotator::ZeroRotator, Params));
	const FTransform PropTransform(FRotator::ZeroRotator, Base + Forward * 3200.f - FVector(0.f, 0.f, 40.f));
	if (AMishpachaStreetProp* Prop = GetWorld()->SpawnActorDeferred<AMishpachaStreetProp>(AMishpachaStreetProp::StaticClass(), PropTransform))
	{
		Prop->Setup(EMishpachaPropKind::TrafficLight, 0);
		Prop->FinishSpawning(PropTransform);
	}
}

void AMishpachaPlayerController::Mish_Aim(int32 bAim)
{
	if (AMishpachaCharacter* MyCharacter = GetMishpachaCharacter())
	{
		MyCharacter->SetAiming(bAim != 0);
	}
}

void AMishpachaPlayerController::Mish_Look(float RelativeYaw, float Pitch)
{
	// Camera around the pawn (180 = look at its face), for screenshots.
	if (const APawn* MyPawn = GetPawn())
	{
		SetControlRotation(FRotator(Pitch, MyPawn->GetActorRotation().Yaw + RelativeYaw, 0.f));
	}
}

void AMishpachaPlayerController::Mish_Wheel(int32 bOpen)
{
	if (bOpen != 0)
	{
		HandleWheelStarted();
	}
	else
	{
		HandleWheelCompleted();
	}
}

void AMishpachaPlayerController::Mish_SpawnRivals(int32 Count)
{
	APawn* MyPawn = GetPawn();
	if (!HasAuthority() || !MyPawn)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector Forward = FRotator(0.f, GetControlRotation().Yaw, 0.f).Vector();
	for (int32 Index = 0; Index < FMath::Clamp(Count, 1, 8); ++Index)
	{
		const FVector Spot = MyPawn->GetActorLocation() + Forward * 1500.f + FVector(Forward.Y, -Forward.X, 0.f) * ((Index - (Count - 1) * 0.5f) * 250.f);
		GetWorld()->SpawnActor<AMishpachaRival>(AMishpachaRival::StaticClass(), Spot + FVector(0.f, 0.f, 50.f), (-Forward).Rotation(), Params);
	}
}

void AMishpachaPlayerController::Mish_AutoDrive(float Throttle)
{
	AutoDriveThrottle = Throttle;
	GetWorldTimerManager().ClearTimer(AutoDriveTimer);
	if (Throttle != 0.f)
	{
		GetWorldTimerManager().SetTimer(AutoDriveTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
			{
				Vehicle->SetHandbrake(false);
				Vehicle->SetThrottle(AutoDriveThrottle);
				Vehicle->SetSteering(0.f);
			}
		}), 0.1f, true);
	}
	else if (AMishpachaVehicle* Vehicle = GetMishpachaVehicle())
	{
		Vehicle->SetThrottle(0.f);
	}
}

void AMishpachaPlayerController::Mish_Switch(int32 Index)
{
	RequestSwitchSibling(Index);
}

void AMishpachaPlayerController::Mish_Menu()
{
	SetMenuOpen(true, EMishpachaMenuPage::Main);
}

void AMishpachaPlayerController::Mish_SkipStep()
{
	if (UMissionSubsystem* Missions = HasAuthority() ? GetWorld()->GetSubsystem<UMissionSubsystem>() : nullptr)
	{
		Missions->SkipObjective();
	}
}

void AMishpachaPlayerController::Mish_Hour(float Hour)
{
	if (AMishpachaDayNight* DayNight = HasAuthority() ? AMishpachaDayNight::Find(this) : nullptr)
	{
		DayNight->SetTimeOfDay(Hour);
	}
}

void AMishpachaPlayerController::HandleMapToggle()
{
	if (!bMenuOpen)
	{
		SetMapOpen(!bMapOpen);
	}
}

void AMishpachaPlayerController::HandleMenuToggle()
{
	AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>();
	if (bMenuOpen && MishpachaHUD)
	{
		MishpachaHUD->MenuBack(); // back a page, or resume from the pause menu
	}
	else if (bMapOpen)
	{
		SetMapOpen(false);
	}
	else
	{
		SetMenuOpen(true, EMishpachaMenuPage::Pause);
	}
}

void AMishpachaPlayerController::HandleMenuSelect()
{
	if (AMishpachaHUD* MishpachaHUD = bMenuOpen ? GetHUD<AMishpachaHUD>() : nullptr)
	{
		MishpachaHUD->MenuActivate();
	}
}

void AMishpachaPlayerController::HandleMenuClick()
{
	float MouseX = 0.f;
	float MouseY = 0.f;
	AMishpachaHUD* MishpachaHUD = bMenuOpen ? GetHUD<AMishpachaHUD>() : nullptr;
	if (MishpachaHUD && GetMousePosition(MouseX, MouseY))
	{
		MishpachaHUD->MenuClick(FVector2D(MouseX, MouseY));
	}
}

void AMishpachaPlayerController::HandleMenuUp()
{
	if (AMishpachaHUD* MishpachaHUD = bMenuOpen ? GetHUD<AMishpachaHUD>() : nullptr)
	{
		MishpachaHUD->MenuNavigate(-1);
	}
}

void AMishpachaPlayerController::HandleMenuDown()
{
	if (AMishpachaHUD* MishpachaHUD = bMenuOpen ? GetHUD<AMishpachaHUD>() : nullptr)
	{
		MishpachaHUD->MenuNavigate(1);
	}
}

void AMishpachaPlayerController::HandleMapSelect()
{
	float MouseX = 0.f;
	float MouseY = 0.f;
	AMishpachaHUD* MishpachaHUD = GetHUD<AMishpachaHUD>();
	if (bMapOpen && MishpachaHUD && GetMousePosition(MouseX, MouseY))
	{
		MishpachaHUD->HandleMapClick(FVector2D(MouseX, MouseY));
	}
}

void AMishpachaPlayerController::HandleMapClear()
{
	if (AMishpachaHUD* MishpachaHUD = bMapOpen ? GetHUD<AMishpachaHUD>() : nullptr)
	{
		MishpachaHUD->ClearWaypoint();
	}
}

void AMishpachaPlayerController::HandleMapPan(const FInputActionValue& Value)
{
	if (AMishpachaHUD* MishpachaHUD = bMapOpen ? GetHUD<AMishpachaHUD>() : nullptr)
	{
		MishpachaHUD->PanMap(Value.Get<FVector2D>(), GetWorld()->GetDeltaSeconds());
	}
}

void AMishpachaPlayerController::HandleMapZoom(const FInputActionValue& Value)
{
	if (AMishpachaHUD* MishpachaHUD = bMapOpen ? GetHUD<AMishpachaHUD>() : nullptr)
	{
		MishpachaHUD->ZoomMap(Value.Get<float>());
	}
}

#undef LOCTEXT_NAMESPACE
