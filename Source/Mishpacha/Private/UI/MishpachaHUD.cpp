// MISHPACHA - HUD actor.
#include "UI/MishpachaHUD.h"
#include "UI/MishpachaHUDWidget.h"
#include "Core/MishpachaGameState.h"
#include "Core/MishpachaPlayerState.h"
#include "Characters/MishpachaCharacter.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Family/FamilySubsystem.h"
#include "Heat/HeatComponent.h"
#include "Abilities/SiblingAbilityComponent.h"
#include "Missions/MissionStartMarker.h"
#include "World/CityGreyboxGenerator.h"
#include "World/MishpachaRouteGuide.h"
#include "World/MishpachaBusiness.h"
#include "GameFramework/GameStateBase.h"
#include "World/MishpachaDayNight.h"
#include "Characters/MishpachaWeapon.h"
#include "World/MishpachaSounds.h"
#include "UI/MishpachaText.h"
#include "Sound/SoundBase.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"
#include "GlobalRenderResources.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "Misc/CommandLine.h"
#include "Core/MishpachaGameInstance.h"
#include "Core/MishpachaPlayerController.h"
#include "Save/MishpachaSaveGame.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "MishpachaHUD"

namespace MishpachaHUDStyle
{
	static const FLinearColor Panel(0.012f, 0.013f, 0.018f, 0.78f);
	static const FLinearColor PanelLight(1.f, 1.f, 1.f, 0.08f);
	static const FLinearColor TextMain(0.95f, 0.95f, 0.93f, 1.f);
	static const FLinearColor TextDim(0.62f, 0.64f, 0.68f, 1.f);
	static const FLinearColor Gold(1.f, 0.78f, 0.16f, 1.f);
	static const FLinearColor Money(0.45f, 0.92f, 0.55f, 1.f);
	static const FLinearColor Danger(0.95f, 0.22f, 0.2f, 1.f);
	static const FLinearColor PoliceBlue(0.2f, 0.45f, 1.f, 1.f);
	static const FLinearColor WaypointPurple(0.72f, 0.38f, 1.f, 1.f);
	static const FLinearColor Outline(0.f, 0.f, 0.f, 0.85f);
	static const FLinearColor Info(0.55f, 0.82f, 1.f, 1.f);

	static FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}
}

// ---------------------------------------------------------------------------
// Lifetime + event forwarding
// ---------------------------------------------------------------------------

void AMishpachaHUD::BeginPlay()
{
	Super::BeginPlay();

	UIFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	MishpachaText::ApplySavedLanguage();

	if (HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UMishpachaHUDWidget>(GetOwningPlayerController(), HUDWidgetClass);
		if (HUDWidget)
		{
			HUDWidget->AddToViewport();
		}
	}

	// The route on the road is local to this player.
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	RouteGuide = GetWorld()->SpawnActor<AMishpachaRouteGuide>(AMishpachaRouteGuide::StaticClass(), FTransform::Identity, Params);

	// PlayerState / pawn / game state arrive at different times on clients - keep (re)binding.
	GetWorldTimerManager().SetTimer(BindTimer, this, &AMishpachaHUD::RefreshBindings, 0.5f, true, 0.f);
	GetWorldTimerManager().SetTimer(RouteTimer, this, &AMishpachaHUD::UpdateRoute, 0.25f, true, 0.5f);

	// The city's hum under everything.
	if (USoundBase* Ambience = MishpachaSounds::Get(TEXT("S_City_Ambience")))
	{
		UGameplayStatics::SpawnSound2D(this, Ambience, 0.3f, 1.f, FMath::FRandRange(0.f, 5.f));
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("MishpachaCapture")))
	{
		StartAutomatedCapture();
	}
	else if (bShowMainMenuOnStart && GetNetMode() == NM_Standalone && !FParse::Param(FCommandLine::Get(), TEXT("NoMenu")))
	{
		// Main menu over the (paused) city once everything has spawned.
		FTimerHandle MenuHandle;
		GetWorldTimerManager().SetTimer(MenuHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(GetOwningPlayerController()))
			{
				PC->SetMenuOpen(true, EMishpachaMenuPage::Main);
			}
		}), 0.3f, false);
	}
}

void AMishpachaHUD::StartAutomatedCapture()
{
	// Dev tool (-MishpachaCapture): FPS chart for 25 s (written to the log), then four screenshots
	// (Saved/Screenshots/.../Capture_N.png) turning around the player.
	auto Command = [this](const TCHAR* Cmd)
	{
		return FTimerDelegate::CreateWeakLambda(this, [this, Cmd]()
		{
			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->ConsoleCommand(Cmd);
			}
		});
	};
	// Real-time ticker (keeps going while the game is paused behind a menu).
	auto After = [this](float Delay, const FTimerDelegate& Delegate)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [Delegate](float)
		{
			Delegate.ExecuteIfBound();
			return false; // once
		}), Delay);
	};
	After(15.f, Command(TEXT("startfpschart")));
	After(40.f, Command(TEXT("stopfpschart")));

	// -MishpachaCaptureCmd=Mish_CompleteJob:Rafi_01+Mish_StartJob:Rafi_02 - console commands run at 38 s
	// ('+' between commands, ':' instead of spaces, so the value survives shell quoting and FParse).
	FString Commands;
	if (FParse::Value(FCommandLine::Get(), TEXT("MishpachaCaptureCmd="), Commands))
	{
		// "Wait:N" tokens delay the commands after them by N seconds.
		TArray<FString> Lines;
		Commands.ParseIntoArray(Lines, TEXT("+"));
		float Delay = 38.f;
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("Wait:")))
			{
				Delay += FCString::Atof(*Line.RightChop(5));
				continue;
			}
			if (Line.StartsWith(TEXT("Shot:")))
			{
				// "Shot:Name" - a screenshot Name.png at this point of the script.
				const FString ShotName = Line.RightChop(5) + TEXT(".png");
				After(Delay, FTimerDelegate::CreateWeakLambda(this, [ShotName]()
				{
					FScreenshotRequest::RequestScreenshot(ShotName, false, false);
				}));
				continue;
			}
			const FString ConsoleLine = Line.Replace(TEXT(":"), TEXT(" "));
			After(Delay, FTimerDelegate::CreateWeakLambda(this, [this, ConsoleLine]()
			{
				UE_LOG(LogTemp, Log, TEXT("[Capture] > %s"), *ConsoleLine);
				if (APlayerController* PC = GetOwningPlayerController())
				{
					PC->ConsoleCommand(ConsoleLine);
				}
			}));
		}
	}
	for (int32 Shot = 0; Shot < 4; ++Shot)
	{
		After(42.f + Shot * 3.f, FTimerDelegate::CreateWeakLambda(this, [this, Shot]()
		{
			if (APlayerController* PC = GetOwningPlayerController())
			{
				PC->SetControlRotation(FRotator(Shot == 3 ? -25.f : -4.f, Shot * 90.f + 20.f, 0.f));
			}
		}));
		After(43.5f + Shot * 3.f, FTimerDelegate::CreateWeakLambda(this, [Shot]()
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Capture_%d.png"), Shot), false, false);
		}));
	}
}

void AMishpachaHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindTimer);
	GetWorldTimerManager().ClearTimer(RouteTimer);
	if (RouteGuide)
	{
		RouteGuide->Destroy();
		RouteGuide = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AMishpachaHUD::RefreshBindings()
{
	APlayerController* PC = GetOwningPlayerController();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return;
	}

	// Money + Heat (PlayerState)
	AMishpachaPlayerState* PS = PC->GetPlayerState<AMishpachaPlayerState>();
	if (PS != BoundPlayerState.Get())
	{
		if (AMishpachaPlayerState* OldPS = BoundPlayerState.Get())
		{
			OldPS->OnMoneyChanged.RemoveDynamic(this, &AMishpachaHUD::HandleMoneyChanged);
			if (UHeatComponent* OldHeat = OldPS->GetHeatComponent())
			{
				OldHeat->OnHeatChanged.RemoveDynamic(this, &AMishpachaHUD::HandleHeatChanged);
			}
		}

		BoundPlayerState = PS;
		if (PS)
		{
			PS->OnMoneyChanged.AddUniqueDynamic(this, &AMishpachaHUD::HandleMoneyChanged);
			HandleMoneyChanged(PS->GetMoney());
			if (UHeatComponent* Heat = PS->GetHeatComponent())
			{
				Heat->OnHeatChanged.AddUniqueDynamic(this, &AMishpachaHUD::HandleHeatChanged);
				HandleHeatChanged(Heat->GetHeatLevel(), 0);
			}
		}
	}

	// Health (current pawn, if it's a character)
	AMishpachaCharacter* PawnCharacter = Cast<AMishpachaCharacter>(PC->GetPawn());
	if (PawnCharacter != BoundCharacter.Get())
	{
		if (AMishpachaCharacter* OldCharacter = BoundCharacter.Get())
		{
			OldCharacter->OnHealthChanged.RemoveDynamic(this, &AMishpachaHUD::HandleHealthChanged);
		}
		BoundCharacter = PawnCharacter;
		if (PawnCharacter)
		{
			PawnCharacter->OnHealthChanged.AddUniqueDynamic(this, &AMishpachaHUD::HandleHealthChanged);
			HandleHealthChanged(PawnCharacter->GetHealth(), PawnCharacter->GetMaxHealth());
		}
	}

	// Mission state (GameState)
	if (!bBoundGameState)
	{
		if (AMishpachaGameState* GameStateActor = World->GetGameState<AMishpachaGameState>())
		{
			GameStateActor->OnMissionStateChanged.AddUniqueDynamic(this, &AMishpachaHUD::HandleMissionStateChanged);
			HandleMissionStateChanged(GameStateActor->GetMissionState());
			bBoundGameState = true;
		}
	}

	// Family switching (Story mode; lives on the server, which is this machine in single player)
	if (!bBoundFamily)
	{
		UFamilySubsystem* Family = World->GetSubsystem<UFamilySubsystem>();
		if (Family && Family->IsFamilyInitialized())
		{
			Family->OnSwitchStarted.AddUniqueDynamic(this, &AMishpachaHUD::HandleSwitchStarted);
			Family->OnSiblingSwitched.AddUniqueDynamic(this, &AMishpachaHUD::HandleSiblingSwitched);
			bBoundFamily = true;
		}
	}
}

void AMishpachaHUD::ShowNotification(const FText& Message)
{
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	if (HUDWidget)
	{
		HUDWidget->OnNotification(Message);
	}

	// Job start / pass / fail get a big center banner instead of a toast.
	const FString Text = Message.ToString();
	auto Banner = [&](const FText& Title, const FString& Prefix, const FLinearColor& Color)
	{
		BannerTitle = Title;
		BannerSubtitle = FText::FromString(Text.RightChop(Prefix.Len()).TrimStartAndEnd());
		BannerColor = Color;
		BannerTime = Now;
	};
	if (Text.StartsWith(TEXT("JOB DONE:")))
	{
		Banner(LOCTEXT("BannerPassed", "JOB DONE"), TEXT("JOB DONE:"), MishpachaHUDStyle::Gold);
		return;
	}
	if (Text.StartsWith(TEXT("JOB FAILED:")))
	{
		Banner(LOCTEXT("BannerFailed", "JOB FAILED"), TEXT("JOB FAILED:"), MishpachaHUDStyle::Danger);
		return;
	}
	if (Text.StartsWith(TEXT("JOB:")))
	{
		Banner(LOCTEXT("BannerStarted", "NEW JOB"), TEXT("JOB:"), MishpachaHUDStyle::TextMain);
		return;
	}

	LastNotification = Message;
	LastNotificationTime = Now;
}

void AMishpachaHUD::SetSwitchMenuOpen(bool bOpen)
{
	if (HUDWidget)
	{
		HUDWidget->OnSwitchMenuToggled(bOpen);
	}
}

void AMishpachaHUD::HandleHeatChanged(int32 NewLevel, int32 OldLevel)
{
	if (HUDWidget)
	{
		HUDWidget->OnHeatChanged(NewLevel, OldLevel);
	}
}

void AMishpachaHUD::HandleMoneyChanged(int32 NewMoney)
{
	if (HUDWidget)
	{
		HUDWidget->OnMoneyChanged(NewMoney);
	}
}

void AMishpachaHUD::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	if (HUDWidget)
	{
		HUDWidget->OnHealthChanged(NewHealth, MaxHealth);
	}
}

void AMishpachaHUD::HandleMissionStateChanged(const FReplicatedMissionState& MissionState)
{
	if (HUDWidget)
	{
		HUDWidget->OnObjectiveChanged(MissionState);
	}
}

void AMishpachaHUD::HandleSwitchStarted(int32 TargetIndex)
{
	if (HUDWidget)
	{
		HUDWidget->OnSiblingSwitchStarted(TargetIndex);
	}
}

void AMishpachaHUD::HandleSiblingSwitched(int32 OldIndex, int32 NewIndex)
{
	if (HUDWidget)
	{
		HUDWidget->OnSiblingSwitched(OldIndex, NewIndex);
	}
}

// ---------------------------------------------------------------------------
// Map data, waypoint, GPS
// ---------------------------------------------------------------------------

ACityGreyboxGenerator* AMishpachaHUD::FindGenerator()
{
	if (!Generator.IsValid() && GetWorld())
	{
		for (TActorIterator<ACityGreyboxGenerator> It(GetWorld()); It; ++It)
		{
			Generator = *It;
			break;
		}
	}
	return Generator.Get();
}

void AMishpachaHUD::EnsureMapTexture()
{
	if (MapTexture)
	{
		return;
	}
	const ACityGreyboxGenerator* Gen = FindGenerator();
	if (!Gen || !Gen->IsGeometryBuilt() || Gen->GetMapAreas().Num() == 0)
	{
		return;
	}

	MapBounds = Gen->GetMapBounds();
	const FVector2D BoundsSize = MapBounds.GetSize();
	if (BoundsSize.X <= 0.0 || BoundsSize.Y <= 0.0)
	{
		return;
	}

	// Dark "night GPS" palette: light roads over darker blocks.
	auto Fill = [](EMishpachaMapAreaType Type) -> FColor
	{
		switch (Type)
		{
		case EMishpachaMapAreaType::Sea:      return FColor(16, 40, 62);
		case EMishpachaMapAreaType::Sand:     return FColor(118, 104, 72);
		case EMishpachaMapAreaType::Road:     return FColor(78, 82, 90);
		case EMishpachaMapAreaType::Block:    return FColor(33, 36, 42);
		case EMishpachaMapAreaType::Plaza:    return FColor(38, 60, 44);
		case EMishpachaMapAreaType::Building: return FColor(52, 56, 64);
		case EMishpachaMapAreaType::Tower:    return FColor(48, 66, 86);
		}
		return FColor::Black;
	};
	auto Edge = [](EMishpachaMapAreaType Type) -> FColor
	{
		return Type == EMishpachaMapAreaType::Tower ? FColor(78, 104, 132) : FColor(72, 77, 87);
	};

	const int32 Res = 1024;
	TArray64<uint8> Pixels;
	Pixels.SetNumUninitialized(int64(Res) * Res * 4);
	auto Put = [&Pixels, Res](int32 X, int32 Y, const FColor& Color)
	{
		const int64 Index = (int64(Y) * Res + X) * 4;
		Pixels[Index + 0] = Color.B;
		Pixels[Index + 1] = Color.G;
		Pixels[Index + 2] = Color.R;
		Pixels[Index + 3] = 255;
	};
	const FColor SeaColor = Fill(EMishpachaMapAreaType::Sea);
	for (int32 Y = 0; Y < Res; ++Y)
	{
		for (int32 X = 0; X < Res; ++X)
		{
			Put(X, Y, SeaColor);
		}
	}

	for (const FMishpachaMapArea& Area : Gen->GetMapAreas())
	{
		const int32 X0 = FMath::Clamp(FMath::RoundToInt((Area.Box.Min.X - MapBounds.Min.X) / BoundsSize.X * Res), 0, Res);
		const int32 X1 = FMath::Clamp(FMath::RoundToInt((Area.Box.Max.X - MapBounds.Min.X) / BoundsSize.X * Res), 0, Res);
		const int32 Y0 = FMath::Clamp(FMath::RoundToInt((Area.Box.Min.Y - MapBounds.Min.Y) / BoundsSize.Y * Res), 0, Res);
		const int32 Y1 = FMath::Clamp(FMath::RoundToInt((Area.Box.Max.Y - MapBounds.Min.Y) / BoundsSize.Y * Res), 0, Res);
		const bool bOutlined = Area.Type == EMishpachaMapAreaType::Building || Area.Type == EMishpachaMapAreaType::Tower;
		const FColor FillColor = Fill(Area.Type);
		const FColor EdgeColor = Edge(Area.Type);
		for (int32 Y = Y0; Y < Y1; ++Y)
		{
			for (int32 X = X0; X < X1; ++X)
			{
				const bool bBorder = bOutlined && (X == X0 || Y == Y0 || X == X1 - 1 || Y == Y1 - 1);
				Put(X, Y, bBorder ? EdgeColor : FillColor);
			}
		}
	}

	MapTexture = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8, NAME_None, Pixels);
	if (MapTexture)
	{
		MapTexture->SRGB = true;
		MapTexture->Filter = TF_Bilinear;
		MapTexture->AddressX = TA_Clamp;
		MapTexture->AddressY = TA_Clamp;
		MapTexture->UpdateResource();
		MapViewCenter = MapBounds.GetCenter();
	}
}

FBox2D AMishpachaHUD::GetEffectiveMapBounds() const
{
	if (MapBounds.bIsValid)
	{
		return MapBounds;
	}
	// No gray-box city: a 1 km square around the player.
	const FVector P = GetPlayerLocation();
	return FBox2D(FVector2D(P.X, P.Y) - FVector2D(50000.0), FVector2D(P.X, P.Y) + FVector2D(50000.0));
}

FVector AMishpachaHUD::GetPlayerLocation(float* OutYaw) const
{
	const APlayerController* PC = GetOwningPlayerController();
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		if (OutYaw)
		{
			*OutYaw = Pawn->GetActorRotation().Yaw;
		}
		return Pawn->GetActorLocation();
	}
	if (PC && PC->PlayerCameraManager)
	{
		if (OutYaw)
		{
			*OutYaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
		}
		return PC->PlayerCameraManager->GetCameraLocation();
	}
	return FVector::ZeroVector;
}

FLinearColor AMishpachaHUD::GetSiblingColor(FName SiblingId, const FLinearColor& Fallback) const
{
	if (!SiblingId.IsNone() && GetWorld())
	{
		for (TActorIterator<AMishpachaCharacter> It(GetWorld()); It; ++It)
		{
			if (It->GetSiblingId() == SiblingId)
			{
				return It->GetPlaceholderColor();
			}
		}
	}
	return Fallback;
}

void AMishpachaHUD::SetWaypoint(const FVector& WorldLocation)
{
	const ACityGreyboxGenerator* Gen = FindGenerator();
	Waypoint = Gen ? Gen->SnapToRoad(WorldLocation) : WorldLocation;
	bHasWaypoint = true;
	UpdateRoute();
}

void AMishpachaHUD::ClearWaypoint()
{
	bHasWaypoint = false;
	UpdateRoute();
}

void AMishpachaHUD::UpdateRoute()
{
	const UWorld* World = GetWorld();
	const AMishpachaGameState* GameStateActor = World ? World->GetGameState<AMishpachaGameState>() : nullptr;
	const FVector PlayerLocation = GetPlayerLocation();

	// Arrived at the waypoint?
	if (bHasWaypoint && FVector::Dist2D(PlayerLocation, Waypoint) < 1000.f)
	{
		bHasWaypoint = false;
		ShowNotification(LOCTEXT("WaypointReached", "Waypoint reached."));
	}

	// The current objective wins over the waypoint.
	FVector Destination;
	bool bHasDestination = false;
	bRouteIsMission = false;
	if (GameStateActor && GameStateActor->GetMissionState().bActive && GameStateActor->GetMissionState().bHasGuide)
	{
		Destination = GameStateActor->GetMissionState().GuideLocation;
		bHasDestination = true;
		bRouteIsMission = true;
		RouteColor = MishpachaHUDStyle::Gold;
	}
	else if (bHasWaypoint)
	{
		Destination = Waypoint;
		bHasDestination = true;
		RouteColor = MishpachaHUDStyle::WaypointPurple;
	}

	RoutePoints.Reset();
	if (!bHasDestination || FVector::Dist2D(PlayerLocation, Destination) < 400.f)
	{
		if (RouteGuide)
		{
			RouteGuide->HideRoute();
		}
		return;
	}

	TArray<FVector> RoadPoints;
	const ACityGreyboxGenerator* Gen = FindGenerator();
	if (Gen && Gen->FindRoadRoute(PlayerLocation, Destination, RoadPoints))
	{
		// Finish at the real destination when it isn't on the road center line.
		if (FVector::Dist2D(RoadPoints.Last(), Destination) > 300.f)
		{
			RoadPoints.Add(FVector(Destination.X, Destination.Y, RoadPoints.Last().Z));
		}
		if (RouteGuide && bShowRouteOnRoad)
		{
			RouteGuide->ShowRoute(RoadPoints, RouteColor, !bRouteIsMission);
		}
		RoutePoints.Add(PlayerLocation);
		RoutePoints.Append(RoadPoints);
	}
	else
	{
		// No street grid: straight line on the maps only.
		if (RouteGuide)
		{
			RouteGuide->HideRoute();
		}
		RoutePoints.Add(PlayerLocation);
		RoutePoints.Add(Destination);
	}
}

void AMishpachaHUD::TrackMissionState()
{
	const AMishpachaGameState* GameStateActor = GetWorld() ? GetWorld()->GetGameState<AMishpachaGameState>() : nullptr;
	if (!GameStateActor)
	{
		return;
	}
	const FReplicatedMissionState& Mission = GameStateActor->GetMissionState();
	const double Now = GetWorld()->GetRealTimeSeconds();
	const FName MissionId = Mission.bActive ? Mission.MissionId : NAME_None;
	if (MissionId != ShownMissionId)
	{
		ShownMissionId = MissionId;
		MissionShownTime = Now;
		ShownObjectiveIndex = Mission.ObjectiveIndex;
		ObjectiveChangedTime = Now;
		UpdateRoute();
	}
	else if (Mission.bActive && Mission.ObjectiveIndex != ShownObjectiveIndex)
	{
		ShownObjectiveIndex = Mission.ObjectiveIndex;
		ObjectiveChangedTime = Now;
		UpdateRoute();
	}
}

void AMishpachaHUD::SetMapOpen(bool bOpen)
{
	bMapOpen = bOpen;
	if (bOpen)
	{
		MapZoom = 1.f;
		MapViewCenter = GetEffectiveMapBounds().GetCenter();
	}
}

void AMishpachaHUD::ClampMapView()
{
	const FBox2D Bounds = GetEffectiveMapBounds();
	const FVector2D Half = Bounds.GetSize() / MapZoom * 0.5;
	MapViewCenter.X = FMath::Clamp(MapViewCenter.X, Bounds.Min.X + Half.X, Bounds.Max.X - Half.X);
	MapViewCenter.Y = FMath::Clamp(MapViewCenter.Y, Bounds.Min.Y + Half.Y, Bounds.Max.Y - Half.Y);
}

void AMishpachaHUD::PanMap(const FVector2D& Axis, float DeltaSeconds)
{
	const FVector2D ViewSize = GetEffectiveMapBounds().GetSize() / MapZoom;
	// Up on the stick / W = up on the screen = -Y in the world.
	MapViewCenter += FVector2D(Axis.X, -Axis.Y) * ViewSize.X * 0.6 * DeltaSeconds;
	ClampMapView();
}

void AMishpachaHUD::ZoomMap(float Steps)
{
	const float OldZoom = MapZoom;
	MapZoom = FMath::Clamp(MapZoom * FMath::Pow(1.25f, Steps), 1.f, 8.f);
	if (FMath::IsNearlyEqual(OldZoom, MapZoom))
	{
		return;
	}

	// Keep the world point under the cursor where it is.
	float MouseX = 0.f;
	float MouseY = 0.f;
	const APlayerController* PC = GetOwningPlayerController();
	if (PC && PC->GetMousePosition(MouseX, MouseY) && FullMapScreenSize.X > 0.0)
	{
		const FVector2D Mouse(MouseX, MouseY);
		const FVector2D Rel = (Mouse - FullMapScreenMin) / FullMapScreenSize;
		if (Rel.X >= 0.0 && Rel.X <= 1.0 && Rel.Y >= 0.0 && Rel.Y <= 1.0)
		{
			const FVector2D WorldUnderMouse = FullMapWorldMin + Rel * FullMapWorldSize;
			const FVector2D NewWorldSize = GetEffectiveMapBounds().GetSize() / MapZoom;
			MapViewCenter = WorldUnderMouse - (Rel - FVector2D(0.5)) * NewWorldSize;
		}
	}
	ClampMapView();
}

void AMishpachaHUD::HandleMapClick(const FVector2D& ScreenPosition)
{
	if (!bMapOpen || FullMapScreenSize.X <= 0.0)
	{
		return;
	}
	const FVector2D Rel = (ScreenPosition - FullMapScreenMin) / FullMapScreenSize;
	if (Rel.X < 0.0 || Rel.X > 1.0 || Rel.Y < 0.0 || Rel.Y > 1.0)
	{
		return;
	}

	// Clicking the existing waypoint removes it.
	if (bHasWaypoint)
	{
		const FVector2D WaypointScreen = FullMapScreenMin + (FVector2D(Waypoint.X, Waypoint.Y) - FullMapWorldMin) / FullMapWorldSize * FullMapScreenSize;
		if (FVector2D::Distance(WaypointScreen, ScreenPosition) < S(18.f))
		{
			ClearWaypoint();
			return;
		}
	}

	const FVector2D World2D = FullMapWorldMin + Rel * FullMapWorldSize;
	SetWaypoint(FVector(World2D.X, World2D.Y, GetPlayerLocation().Z));
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

FVector2D AMishpachaHUD::FMapView::ToScreen(const FVector& World) const
{
	return ScreenMin + (FVector2D(World.X, World.Y) - WorldMin) / WorldSize * ScreenSize;
}

FVector AMishpachaHUD::FMapView::ToWorld(const FVector2D& Screen) const
{
	const FVector2D W = WorldMin + (Screen - ScreenMin) / ScreenSize * WorldSize;
	return FVector(W.X, W.Y, 0.0);
}

bool AMishpachaHUD::FMapView::Contains(const FVector2D& Screen, float Inset) const
{
	return Screen.X >= ScreenMin.X + Inset && Screen.Y >= ScreenMin.Y + Inset
		&& Screen.X <= ScreenMin.X + ScreenSize.X - Inset && Screen.Y <= ScreenMin.Y + ScreenSize.Y - Inset;
}

FVector2D AMishpachaHUD::FMapView::ClampToEdge(const FVector2D& Screen, float Inset) const
{
	return FVector2D(
		FMath::Clamp(Screen.X, ScreenMin.X + Inset, ScreenMin.X + ScreenSize.X - Inset),
		FMath::Clamp(Screen.Y, ScreenMin.Y + Inset, ScreenMin.Y + ScreenSize.Y - Inset));
}

FSlateFontInfo AMishpachaHUD::Font(float Size, bool bBold) const
{
	// Roboto + Noto Sans Hebrew when the Hebrew faces are in the project.
	return MishpachaText::MakeFont(UIFont.Get(), FMath::Max(1, FMath::RoundToInt(Size * UIScale)), bBold);
}

FVector2D AMishpachaHUD::MeasureText(const FText& Text, const FSlateFontInfo& FontInfo) const
{
	if (FSlateApplication::IsInitialized())
	{
		if (FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
		{
			return Renderer->GetFontMeasureService()->Measure(Text, FontInfo);
		}
	}
	return FVector2D(Text.ToString().Len() * FontInfo.Size * 0.62f, FontInfo.Size * 1.35f);
}

void AMishpachaHUD::DrawLabel(const FText& InText, float X, float Y, const FSlateFontInfo& FontInfo, const FLinearColor& Color, float HAlign, bool bShadow)
{
	if (!Canvas || InText.IsEmpty() || !UIFont)
	{
		return;
	}
	// The canvas draws left to right: Hebrew lines go in visual order.
	const FString Source = InText.ToString();
	const FText Text = MishpachaText::HasHebrew(Source) ? FText::FromString(MishpachaText::ToVisualOrder(Source)) : InText;
	FVector2D Position(X, Y);
	if (HAlign > 0.f)
	{
		Position.X -= MeasureText(Text, FontInfo).X * HAlign;
	}
	FCanvasTextItem Item(Position, Text, FontInfo, Color);
	if (bShadow)
	{
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.8f * Color.A), FVector2D(1.0, 1.0));
	}
	Canvas->DrawItem(Item);
}

void AMishpachaHUD::FillRect(float X, float Y, float W, float H, const FLinearColor& Color)
{
	if (!Canvas || W <= 0.f || H <= 0.f)
	{
		return;
	}
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AMishpachaHUD::AddTri(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
	FCanvasUVTri& Tri = Tris.AddDefaulted_GetRef();
	Tri.V0_Pos = A;
	Tri.V1_Pos = B;
	Tri.V2_Pos = C;
	Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
	Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = Color;
}

void AMishpachaHUD::AddQuad(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Color)
{
	AddTri(Tris, A, B, C, Color);
	AddTri(Tris, A, C, D, Color);
}

void AMishpachaHUD::DrawTris(const TArray<FCanvasUVTri>& Tris)
{
	if (!Canvas || Tris.Num() == 0)
	{
		return;
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AMishpachaHUD::DrawDisc(const FVector2D& Center, float Radius, const FLinearColor& Color, int32 Segments)
{
	TArray<FCanvasUVTri> Tris;
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A0 = 2.f * PI * Index / Segments;
		const float A1 = 2.f * PI * (Index + 1) / Segments;
		AddTri(Tris, Center, Center + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius, Center + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius, Color);
	}
	DrawTris(Tris);
}

void AMishpachaHUD::DrawRing(const FVector2D& Center, float Radius, float Thickness, const FLinearColor& Color, int32 Segments)
{
	TArray<FCanvasUVTri> Tris;
	const float Inner = FMath::Max(0.f, Radius - Thickness);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float A0 = 2.f * PI * Index / Segments;
		const float A1 = 2.f * PI * (Index + 1) / Segments;
		const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
		const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
		AddQuad(Tris, Center + D0 * Inner, Center + D0 * Radius, Center + D1 * Radius, Center + D1 * Inner, Color);
	}
	DrawTris(Tris);
}

void AMishpachaHUD::DrawThickLine(const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color)
{
	const FVector2D Delta = B - A;
	const double Length = Delta.Size();
	if (Length < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector2D Side = FVector2D(-Delta.Y, Delta.X) / Length * (Thickness * 0.5f);
	TArray<FCanvasUVTri> Tris;
	AddQuad(Tris, A - Side, B - Side, B + Side, A + Side, Color);
	DrawTris(Tris);
}

void AMishpachaHUD::DrawPlayerArrow(const FVector2D& Center, float Yaw, float Size, const FLinearColor& Color)
{
	// Screen X = world X, screen Y = world Y, so the world yaw maps straight onto the screen.
	const float Rad = FMath::DegreesToRadians(Yaw);
	const FVector2D Dir(FMath::Cos(Rad), FMath::Sin(Rad));
	const FVector2D Side(-Dir.Y, Dir.X);
	auto Arrow = [&](float Scale, const FLinearColor& FillColor)
	{
		const float R = Size * Scale;
		const FVector2D Tip = Center + Dir * R;
		const FVector2D Left = Center - Dir * (R * 0.75f) + Side * (R * 0.75f);
		const FVector2D Right = Center - Dir * (R * 0.75f) - Side * (R * 0.75f);
		const FVector2D Notch = Center - Dir * (R * 0.3f);
		TArray<FCanvasUVTri> Tris;
		AddTri(Tris, Tip, Left, Notch, FillColor);
		AddTri(Tris, Tip, Notch, Right, FillColor);
		DrawTris(Tris);
	};
	Arrow(1.35f, MishpachaHUDStyle::Outline);
	Arrow(1.f, Color);
}

void AMishpachaHUD::DrawStar(const FVector2D& Center, float Radius, const FLinearColor& Color)
{
	TArray<FCanvasUVTri> Tris;
	FVector2D Points[10];
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const float Angle = -PI * 0.5f + PI * Index / 5.f;
		const float R = (Index % 2 == 0) ? Radius : Radius * 0.45f;
		Points[Index] = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R;
	}
	for (int32 Index = 0; Index < 10; ++Index)
	{
		AddTri(Tris, Center, Points[Index], Points[(Index + 1) % 10], Color);
	}
	DrawTris(Tris);
}

void AMishpachaHUD::DrawDiamond(const FVector2D& Center, float Radius, const FLinearColor& Color)
{
	TArray<FCanvasUVTri> Tris;
	AddQuad(Tris, Center + FVector2D(0.f, -Radius), Center + FVector2D(Radius, 0.f), Center + FVector2D(0.f, Radius), Center + FVector2D(-Radius, 0.f), Color);
	DrawTris(Tris);
}

void AMishpachaHUD::DrawPolylineClipped(const TArray<FVector>& WorldPoints, const FMapView& View, float Thickness, const FLinearColor& Color)
{
	const FVector2D Min = View.ScreenMin;
	const FVector2D Max = View.ScreenMin + View.ScreenSize;
	for (int32 Index = 1; Index < WorldPoints.Num(); ++Index)
	{
		FVector2D A = View.ToScreen(WorldPoints[Index - 1]);
		FVector2D B = View.ToScreen(WorldPoints[Index]);

		// Liang-Barsky clip against the map frame.
		const FVector2D D = B - A;
		double T0 = 0.0;
		double T1 = 1.0;
		const double P[4] = { -D.X, D.X, -D.Y, D.Y };
		const double Q[4] = { A.X - Min.X, Max.X - A.X, A.Y - Min.Y, Max.Y - A.Y };
		bool bVisible = true;
		for (int32 Edge = 0; Edge < 4 && bVisible; ++Edge)
		{
			if (FMath::IsNearlyZero(P[Edge]))
			{
				bVisible = Q[Edge] >= 0.0;
			}
			else
			{
				const double T = Q[Edge] / P[Edge];
				if (P[Edge] < 0.0) { T0 = FMath::Max(T0, T); }
				else { T1 = FMath::Min(T1, T); }
				bVisible = T0 <= T1;
			}
		}
		if (!bVisible)
		{
			continue;
		}
		const FVector2D ClippedA = A + D * T0;
		const FVector2D ClippedB = A + D * T1;
		DrawThickLine(ClippedA, ClippedB, Thickness, Color);
		DrawDisc(ClippedB, Thickness * 0.5f, Color, 10); // round the joints
	}
}

// ---------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------

void AMishpachaHUD::DrawMapLayer(const FMapView& View)
{
	if (!MapTexture || !MapTexture->GetResource())
	{
		FillRect(View.ScreenMin.X, View.ScreenMin.Y, View.ScreenSize.X, View.ScreenSize.Y, FLinearColor(0.035f, 0.04f, 0.05f, 0.95f));
		return;
	}
	const FVector2D BoundsSize = MapBounds.GetSize();
	const FVector2D UV0 = (View.WorldMin - MapBounds.Min) / BoundsSize;
	const FVector2D UV1 = (View.WorldMin + View.WorldSize - MapBounds.Min) / BoundsSize;
	FCanvasTileItem Tile(View.ScreenMin, MapTexture->GetResource(), View.ScreenSize, UV0, UV1, FLinearColor::White);
	Tile.BlendMode = SE_BLEND_Opaque;
	Canvas->DrawItem(Tile);
}

void AMishpachaHUD::DrawMapContents(const FMapView& View, bool bFullMap)
{
	UWorld* World = GetWorld();
	const APlayerController* PC = GetOwningPlayerController();
	const APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	const AMishpachaGameState* GameStateActor = World->GetGameState<AMishpachaGameState>();
	const bool bMissionActive = GameStateActor && GameStateActor->GetMissionState().bActive;
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	const int32 HeatLevel = (PS && PS->GetHeatComponent()) ? PS->GetHeatComponent()->GetHeatLevel() : 0;
	const float Time = World->GetRealTimeSeconds();
	const float Pulse = 0.5f + 0.5f * FMath::Sin(Time * 5.f);
	const float Blip = S(bFullMap ? 7.f : 6.f);

	// Place names (full map only).
	if (bFullMap)
	{
		if (const ACityGreyboxGenerator* Gen = Generator.Get())
		{
			for (const FMishpachaMapPlace& Place : Gen->GetMapPlaces())
			{
				const FVector2D P = View.ToScreen(Place.Location);
				if (View.Contains(P, S(4.f)))
				{
					DrawDisc(P, S(3.f), FLinearColor(1.f, 1.f, 1.f, 0.7f), 10);
					DrawLabel(Place.Label, P.X, P.Y + S(6.f), Font(10.f, true), FLinearColor(0.88f, 0.89f, 0.9f, 0.9f), 0.5f);
				}
			}
		}
	}

	// GPS route.
	if (RoutePoints.Num() > 1)
	{
		DrawPolylineClipped(RoutePoints, View, S(bFullMap ? 7.f : 6.f), MishpachaHUDStyle::WithAlpha(FLinearColor::Black, 0.55f));
		DrawPolylineClipped(RoutePoints, View, S(bFullMap ? 4.f : 3.5f), RouteColor);
	}

	// Rafi's businesses: green for sale, gold his, red under attack (always shown when under attack).
	for (TActorIterator<AMishpachaBusiness> It(World); It; ++It)
	{
		const EMishpachaBusinessState State = It->GetState();
		if (State != EMishpachaBusinessState::UnderAttack && bMissionActive)
		{
			continue;
		}
		const FVector2D P = View.ToScreen(It->GetActorLocation());
		if (!View.Contains(P, Blip))
		{
			continue;
		}
		const FLinearColor Color = State == EMishpachaBusinessState::UnderAttack ? FLinearColor(0.95f, 0.1f, 0.08f)
			: (State == EMishpachaBusinessState::Owned ? MishpachaHUDStyle::Gold : FLinearColor(0.2f, 0.75f, 0.35f));
		DrawDisc(P, Blip * 1.2f, MishpachaHUDStyle::Outline);
		DrawDisc(P, Blip, Color);
		DrawLabel(LOCTEXT("BusinessBlip", "$"), P.X, P.Y - S(bFullMap ? 8.f : 7.f), Font(bFullMap ? 10.f : 8.5f, true), FLinearColor::White, 0.5f, false);
		if (bFullMap)
		{
			DrawLabel(It->GetDisplayName(), P.X + Blip * 1.8f, P.Y - S(8.f), Font(10.f), Color);
		}
	}

	// Job markers (hidden while a job runs, like the beacons' job).
	if (!bMissionActive)
	{
		for (TActorIterator<AMissionStartMarker> It(World); It; ++It)
		{
			if (It->IsHidden())
			{
				continue;
			}
			const FVector2D P = View.ToScreen(It->GetActorLocation());
			if (!View.Contains(P, Blip))
			{
				continue;
			}
			const FString Label = It->GetMarkerText().ToString();
			DrawDisc(P, Blip * 1.45f, MishpachaHUDStyle::Outline);
			DrawDisc(P, Blip * 1.2f, It->GetMarkerColor());
			if (!Label.IsEmpty())
			{
				DrawLabel(FText::FromString(Label.Left(1)), P.X, P.Y - S(bFullMap ? 8.f : 7.f), Font(bFullMap ? 10.f : 8.5f, true), FLinearColor::White, 0.5f, false);
			}
			if (bFullMap)
			{
				DrawLabel(It->GetMarkerText(), P.X + Blip * 1.8f, P.Y - S(8.f), Font(10.f), It->GetMarkerColor());
			}
		}
	}

	// Cars nobody is driving: small rectangles pointing the way the car faces.
	{
		TArray<FCanvasUVTri> CarTris;
		const float HalfLength = FMath::Max(S(bFullMap ? 4.f : 5.f), 225.f / View.WorldSize.X * View.ScreenSize.X);
		const float HalfWidth = HalfLength * 0.48f;
		const FLinearColor CarColor(0.78f, 0.8f, 0.86f, 0.95f);
		for (TActorIterator<AMishpachaVehicle> It(World); It; ++It)
		{
			if (*It == PlayerPawn || It->HasDriver())
			{
				continue;
			}
			const FVector2D P = View.ToScreen(It->GetActorLocation());
			if (!View.Contains(P, HalfLength))
			{
				continue;
			}
			const float Rad = FMath::DegreesToRadians(It->GetActorRotation().Yaw);
			const FVector2D Fwd = FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
			const FVector2D Side(-Fwd.Y, Fwd.X);
			const FVector2D F = Fwd * HalfLength;
			const FVector2D R = Side * HalfWidth;
			const FVector2D FO = Fwd * (HalfLength + 1.5f); // outline, ~1.5 px wider
			const FVector2D RO = Side * (HalfWidth + 1.5f);
			AddQuad(CarTris, P - FO - RO, P + FO - RO, P + FO + RO, P - FO + RO, MishpachaHUDStyle::Outline);
			AddQuad(CarTris, P - F - R, P + F - R, P + F + R, P - F + R, CarColor);
		}
		DrawTris(CarTris);
	}

	// People: the other siblings, family soldiers, police.
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Pawn = *It;
		if (Pawn == PlayerPawn)
		{
			continue;
		}
		const AMishpachaCharacter* AsCharacter = Cast<AMishpachaCharacter>(Pawn);
		if (AsCharacter && AsCharacter->IsDead())
		{
			continue;
		}
		const FVector2D P = View.ToScreen(Pawn->GetActorLocation());
		if (!View.Contains(P, Blip * 0.5f))
		{
			continue;
		}

		if (Pawn->ActorHasTag(FName(TEXT("Police"))))
		{
			if (HeatLevel > 0 || bFullMap)
			{
				const bool bFlash = HeatLevel > 0 && FMath::Fmod(Time * 3.f, 1.f) < 0.5f;
				DrawDisc(P, Blip * 0.85f, MishpachaHUDStyle::Outline, 12);
				DrawDisc(P, Blip * 0.65f, bFlash ? MishpachaHUDStyle::Danger : MishpachaHUDStyle::PoliceBlue, 12);
			}
		}
		else if (Pawn->ActorHasTag(FName(TEXT("FamilyBackup"))))
		{
			DrawDisc(P, Blip * 0.6f, FLinearColor(0.45f, 0.85f, 0.45f, 0.95f), 10);
		}
		else if (AsCharacter && !AsCharacter->GetSiblingId().IsNone())
		{
			DrawDisc(P, Blip * 1.1f, MishpachaHUDStyle::Outline);
			DrawDisc(P, Blip * 0.85f, AsCharacter->GetPlaceholderColor());
			if (bFullMap)
			{
				DrawLabel(MishpachaText::SiblingName(AsCharacter->GetSiblingId(), false), P.X + Blip * 1.5f, P.Y - S(8.f), Font(10.f, true), AsCharacter->GetPlaceholderColor());
			}
		}
	}

	// Waypoint (clamped to the minimap edge when it's off-screen).
	if (bHasWaypoint)
	{
		FVector2D P = View.ToScreen(Waypoint);
		if (!bFullMap && !View.Contains(P, Blip))
		{
			P = View.ClampToEdge(P, Blip * 1.2f);
		}
		if (View.Contains(P))
		{
			DrawDiamond(P, Blip * 1.55f, MishpachaHUDStyle::Outline);
			DrawDiamond(P, Blip * 1.2f, MishpachaHUDStyle::WaypointPurple);
		}
	}

	// Current objective.
	if (bMissionActive && GameStateActor->GetMissionState().bHasGuide)
	{
		FVector2D P = View.ToScreen(GameStateActor->GetMissionState().GuideLocation);
		if (!bFullMap && !View.Contains(P, Blip))
		{
			P = View.ClampToEdge(P, Blip * 1.2f);
		}
		if (View.Contains(P))
		{
			DrawRing(P, Blip * (1.6f + Pulse * 0.9f), S(2.f), MishpachaHUDStyle::WithAlpha(MishpachaHUDStyle::Gold, 1.f - Pulse * 0.6f));
			DrawDisc(P, Blip * 1.25f, MishpachaHUDStyle::Outline);
			DrawDisc(P, Blip, MishpachaHUDStyle::Gold);
		}
	}

	// The player, last so it's always on top.
	float Yaw = 0.f;
	const FVector PlayerLocation = GetPlayerLocation(&Yaw);
	const FVector2D PlayerScreen = View.ToScreen(PlayerLocation);
	if (View.Contains(PlayerScreen))
	{
		const AMishpachaCharacter* PlayerCharacter = Cast<AMishpachaCharacter>(PlayerPawn);
		DrawPlayerArrow(PlayerScreen, Yaw, S(bFullMap ? 10.f : 9.f), PlayerCharacter ? PlayerCharacter->GetPlaceholderColor() * 1.3f : FLinearColor::White);
	}
}

void AMishpachaHUD::DrawMinimap()
{
	const float Size = S(270.f);
	const float X = S(28.f);
	const float Y = S(28.f);
	const float Border = S(3.f);

	const APlayerController* PC = GetOwningPlayerController();
	const bool bInVehicle = PC && Cast<AMishpachaVehicle>(PC->GetPawn());
	const float Range = bInVehicle ? MinimapRangeInVehicle : MinimapRangeOnFoot;
	const FVector Center = GetPlayerLocation();

	// Frame: drop shadow, border (flashes red / blue while wanted).
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	const int32 HeatLevel = (PS && PS->GetHeatComponent()) ? PS->GetHeatComponent()->GetHeatLevel() : 0;
	FLinearColor BorderColor(0.f, 0.f, 0.f, 0.85f);
	if (HeatLevel > 0)
	{
		BorderColor = FMath::Fmod(GetWorld()->GetRealTimeSeconds() * 2.f, 1.f) < 0.5f ? MishpachaHUDStyle::Danger : MishpachaHUDStyle::PoliceBlue;
	}
	FillRect(X - Border + S(4.f), Y - Border + S(5.f), Size + Border * 2.f, Size + Border * 2.f, FLinearColor(0.f, 0.f, 0.f, 0.35f));
	FillRect(X - Border, Y - Border, Size + Border * 2.f, Size + Border * 2.f, BorderColor);

	FMapView View;
	View.ScreenMin = FVector2D(X, Y);
	View.ScreenSize = FVector2D(Size, Size);
	View.WorldSize = FVector2D(Range, Range);
	View.WorldMin = FVector2D(Center.X, Center.Y) - View.WorldSize * 0.5;
	DrawMapLayer(View);

	// Soft vignette at the top so the "N" reads.
	FillRect(X, Y, Size, S(22.f), FLinearColor(0.f, 0.f, 0.f, 0.25f));
	DrawMapContents(View, false);

	DrawLabel(LOCTEXT("North", "N"), X + Size * 0.5f, Y + S(3.f), Font(10.f, true), FLinearColor(1.f, 1.f, 1.f, 0.85f), 0.5f);
	if (const AMishpachaDayNight* DayNight = AMishpachaDayNight::Find(this))
	{
		const float Hour = DayNight->GetTimeOfDay();
		const int32 Minutes = FMath::FloorToInt(Hour * 60.f);
		const FText Clock = FText::FromString(FString::Printf(TEXT("%02d:%02d"), (Minutes / 60) % 24, Minutes % 60));
		DrawLabel(Clock, X + S(8.f), Y + S(3.f), Font(10.f, true), FLinearColor(1.f, 1.f, 1.f, 0.85f));
	}
	DrawLabel(LOCTEXT("MapKeyHint", "M  MAP"), X + Size - S(8.f), Y + Size - S(20.f), Font(8.5f, true), FLinearColor(1.f, 1.f, 1.f, 0.55f), 1.f);

	if (!HUDWidget && bDrawDebugOverlay)
	{
		DrawStatusPanel(X - Border, Y + Size + Border + S(10.f), Size + Border * 2.f);
	}
}

void AMishpachaHUD::DrawFullMap()
{
	const float ClipX = Canvas->ClipX;
	const float ClipY = Canvas->ClipY;
	FillRect(0.f, 0.f, ClipX, ClipY, FLinearColor(0.01f, 0.012f, 0.018f, 0.82f));

	const float LegendW = S(250.f);
	const float Gap = S(24.f);
	const float Top = S(92.f);
	const float Bottom = S(64.f);
	const float Side = FMath::Max(S(200.f), FMath::Min(ClipY - Top - Bottom, ClipX - LegendW - Gap - S(80.f)));
	const float MapX = (ClipX - (Side + Gap + LegendW)) * 0.5f;
	const float MapY = Top;

	// Title.
	DrawLabel(LOCTEXT("MapTitle", "TEL AVIR"), MapX, S(26.f), Font(26.f, true), MishpachaHUDStyle::TextMain);
	DrawLabel(LOCTEXT("MapSubtitle", "CHOF HAZAHAV"), MapX + S(3.f), S(62.f), Font(10.f, true), MishpachaHUDStyle::TextDim);

	ClampMapView();
	FMapView View;
	View.ScreenMin = FVector2D(MapX, MapY);
	View.ScreenSize = FVector2D(Side, Side);
	View.WorldSize = GetEffectiveMapBounds().GetSize() / MapZoom;
	View.WorldMin = MapViewCenter - View.WorldSize * 0.5;
	FullMapScreenMin = View.ScreenMin;
	FullMapScreenSize = View.ScreenSize;
	FullMapWorldMin = View.WorldMin;
	FullMapWorldSize = View.WorldSize;

	FillRect(MapX - S(3.f), MapY - S(3.f), Side + S(6.f), Side + S(6.f), FLinearColor(1.f, 1.f, 1.f, 0.12f));
	DrawMapLayer(View);
	DrawMapContents(View, true);

	// Cursor crosshair + coordinates hint while hovering the map.
	float MouseX = 0.f;
	float MouseY = 0.f;
	const APlayerController* PC = GetOwningPlayerController();
	if (PC && PC->GetMousePosition(MouseX, MouseY) && View.Contains(FVector2D(MouseX, MouseY)))
	{
		const FLinearColor Hair(1.f, 1.f, 1.f, 0.35f);
		FillRect(MapX, MouseY, Side, 1.f, Hair);
		FillRect(MouseX, MapY, 1.f, Side, Hair);
		const float Meters = FVector::Dist2D(GetPlayerLocation(), View.ToWorld(FVector2D(MouseX, MouseY))) / 100.f;
		DrawLabel(FText::Format(LOCTEXT("HoverDistance", "{0} m"), FText::AsNumber(FMath::RoundToInt(Meters))), MouseX + S(10.f), MouseY + S(8.f), Font(10.f, true), MishpachaHUDStyle::TextMain);
	}

	// Legend.
	const float LX = MapX + Side + Gap;
	float LY = MapY;
	FillRect(LX, LY, LegendW, S(340.f), MishpachaHUDStyle::Panel);
	FillRect(LX, LY, LegendW, S(3.f), MishpachaHUDStyle::Gold);
	DrawLabel(LOCTEXT("Legend", "LEGEND"), LX + S(18.f), LY + S(16.f), Font(11.f, true), MishpachaHUDStyle::TextDim);
	LY += S(50.f);
	auto Row = [&](const FText& Label, TFunctionRef<void(const FVector2D&)> Icon)
	{
		Icon(FVector2D(LX + S(28.f), LY + S(9.f)));
		DrawLabel(Label, LX + S(50.f), LY, Font(11.f), MishpachaHUDStyle::TextMain);
		LY += S(30.f);
	};
	Row(LOCTEXT("LegendYou", "You"), [&](const FVector2D& P) { DrawPlayerArrow(P, -90.f, S(9.f), FLinearColor::White); });
	Row(LOCTEXT("LegendJob", "Job (sibling color)"), [&](const FVector2D& P) { DrawDisc(P, S(9.f), MishpachaHUDStyle::Outline); DrawDisc(P, S(7.5f), FLinearColor(0.85f, 0.35f, 0.08f)); });
	Row(LOCTEXT("LegendObjective", "Objective"), [&](const FVector2D& P) { DrawDisc(P, S(8.f), MishpachaHUDStyle::Outline); DrawDisc(P, S(6.5f), MishpachaHUDStyle::Gold); });
	Row(LOCTEXT("LegendWaypoint", "Waypoint"), [&](const FVector2D& P) { DrawDiamond(P, S(9.f), MishpachaHUDStyle::Outline); DrawDiamond(P, S(7.f), MishpachaHUDStyle::WaypointPurple); });
	Row(LOCTEXT("LegendFamily", "Sibling"), [&](const FVector2D& P) { DrawDisc(P, S(7.f), MishpachaHUDStyle::Outline); DrawDisc(P, S(5.5f), FLinearColor(0.05f, 0.45f, 0.45f)); });
	Row(LOCTEXT("LegendCar", "Car"), [&](const FVector2D& P) { FillRect(P.X - S(8.f), P.Y - S(4.5f), S(16.f), S(9.f), MishpachaHUDStyle::Outline); FillRect(P.X - S(7.f), P.Y - S(3.5f), S(14.f), S(7.f), FLinearColor(0.78f, 0.8f, 0.86f, 0.95f)); });
	Row(LOCTEXT("LegendPolice", "Police"), [&](const FVector2D& P) { DrawDisc(P, S(6.f), MishpachaHUDStyle::Outline); DrawDisc(P, S(4.5f), MishpachaHUDStyle::PoliceBlue); });
	Row(LOCTEXT("LegendRoute", "GPS route"), [&](const FVector2D& P) { DrawThickLine(P - FVector2D(S(12.f), 0.f), P + FVector2D(S(12.f), 0.f), S(4.f), MishpachaHUDStyle::Gold); });

	// Waypoint status.
	LY += S(6.f);
	if (bHasWaypoint)
	{
		const float Meters = FVector::Dist2D(GetPlayerLocation(), Waypoint) / 100.f;
		DrawLabel(FText::Format(LOCTEXT("WaypointDistance", "Waypoint: {0} m"), FText::AsNumber(FMath::RoundToInt(Meters))), LX + S(18.f), LY, Font(11.f, true), MishpachaHUDStyle::WaypointPurple);
	}
	else
	{
		DrawLabel(LOCTEXT("NoWaypoint", "Click the map to set a waypoint"), LX + S(18.f), LY, Font(10.f), MishpachaHUDStyle::TextDim);
	}

	// Controls.
	float HintX = MishpachaText::IsRightToLeft() ? MapX + Side : MapX;
	const float HintY = MapY + Side + S(18.f);
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyLMB", "LMB"), LOCTEXT("HintSet", "Set / remove waypoint"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyRMB", "RMB"), LOCTEXT("HintClear", "Clear waypoint"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyWheel", "WHEEL"), LOCTEXT("HintZoom", "Zoom"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyWASD", "WASD"), LOCTEXT("HintPan", "Move map"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyMClose", "M"), LOCTEXT("HintClose", "Close"));
}

// ---------------------------------------------------------------------------
// HUD panels
// ---------------------------------------------------------------------------

void AMishpachaHUD::DrawStatusPanel(float X, float Y, float Width)
{
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	if (!PlayerCharacter && PC)
	{
		// In a car or flying the drone: show the sibling who's driving / piloting.
		if (const AMishpachaVehicle* Vehicle = Cast<AMishpachaVehicle>(PC->GetPawn()))
		{
			for (TActorIterator<AMishpachaCharacter> It(GetWorld()); It; ++It)
			{
				if (It->GetCurrentVehicle() == Vehicle)
				{
					PlayerCharacter = *It;
					break;
				}
			}
		}
	}

	const float H = S(84.f);
	const FLinearColor Accent = PlayerCharacter ? PlayerCharacter->GetPlaceholderColor() * 1.3f : MishpachaHUDStyle::Gold;
	FillRect(X, Y, Width, H, MishpachaHUDStyle::Panel);
	FillRect(X, Y, S(4.f), H, Accent);

	const float Left = X + S(16.f);
	const float Right = X + Width - S(12.f);

	// Name + money.
	const FText Who = PlayerCharacter && !PlayerCharacter->GetSiblingId().IsNone()
		? MishpachaText::SiblingName(PlayerCharacter->GetSiblingId())
		: LOCTEXT("PlayerName", "PLAYER");
	DrawLabel(Who, Left, Y + S(9.f), Font(14.f, true), Accent);
	if (PS)
	{
		DrawLabel(FText::Format(LOCTEXT("MoneyFmt", "NIS {0}"), FText::AsNumber(PS->GetMoney())), Right, Y + S(9.f), Font(14.f, true), MishpachaHUDStyle::Money, 1.f);
	}

	// Health bar.
	const float BarY = Y + S(38.f);
	const float BarW = Right - Left;
	FillRect(Left, BarY, BarW, S(6.f), FLinearColor(1.f, 1.f, 1.f, 0.1f));
	if (PlayerCharacter && PlayerCharacter->GetMaxHealth() > 0.f)
	{
		const float Fraction = FMath::Clamp(PlayerCharacter->GetHealth() / PlayerCharacter->GetMaxHealth(), 0.f, 1.f);
		const FLinearColor HealthColor = Fraction < 0.3f ? MishpachaHUDStyle::Danger : FLinearColor(0.36f, 0.85f, 0.46f, 1.f);
		FillRect(Left, BarY, BarW * Fraction, S(6.f), HealthColor);
	}

	// Heat stars.
	const UHeatComponent* Heat = PS ? PS->GetHeatComponent() : nullptr;
	const int32 HeatLevel = Heat ? Heat->GetHeatLevel() : 0;
	const float StarY = Y + S(63.f);
	const bool bBlink = HeatLevel > 0 && FMath::Fmod(GetWorld()->GetRealTimeSeconds() * 2.5f, 1.f) < 0.7f;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FVector2D P(Left + S(8.f) + Index * S(20.f), StarY);
		const bool bLit = Index < HeatLevel;
		DrawStar(P, S(8.5f), MishpachaHUDStyle::Outline);
		DrawStar(P, S(7.f), bLit ? (bBlink ? FLinearColor(1.f, 0.85f, 0.3f, 1.f) : FLinearColor(1.f, 0.6f, 0.15f, 1.f)) : FLinearColor(1.f, 1.f, 1.f, 0.16f));
	}
	if (HeatLevel > 0 && Heat)
	{
		// Escape progress under the stars.
		FillRect(Left, StarY + S(11.f), S(96.f), S(2.f), FLinearColor(1.f, 1.f, 1.f, 0.12f));
		FillRect(Left, StarY + S(11.f), S(96.f) * FMath::Clamp(Heat->GetEscapeProgress(), 0.f, 1.f), S(2.f), MishpachaHUDStyle::Info);
	}

	// Ability.
	if (const USiblingAbilityComponent* Ability = PlayerCharacter ? PlayerCharacter->GetAbility() : nullptr)
	{
		const float Cooldown = Ability->GetCooldownRemaining();
		FText State;
		FLinearColor StateColor = MishpachaHUDStyle::Money;
		if (Ability->IsAbilityActive())
		{
			State = LOCTEXT("AbilityActive", "ACTIVE");
			StateColor = MishpachaHUDStyle::Gold;
		}
		else if (Cooldown > 0.f)
		{
			State = FText::Format(LOCTEXT("AbilityCooldown", "{0}s"), FText::AsNumber(FMath::CeilToInt(Cooldown)));
			StateColor = MishpachaHUDStyle::TextDim;
		}
		else
		{
			State = LOCTEXT("AbilityReady", "READY");
		}
		const FSlateFontInfo StateFont = Font(10.f, true);
		const float StateW = MeasureText(State, StateFont).X;
		DrawLabel(State, Right, StarY - S(8.f), StateFont, StateColor, 1.f);
		DrawLabel(FText::Format(LOCTEXT("AbilityFmt", "Q  {0}"), FText::FromString(Ability->GetAbilityName().ToString().ToUpper())), Right - StateW - S(8.f), StarY - S(8.f), Font(10.f, true), MishpachaHUDStyle::TextMain, 1.f);
	}
}

void AMishpachaHUD::DrawMissionCard()
{
	const AMishpachaGameState* GameStateActor = GetWorld()->GetGameState<AMishpachaGameState>();
	const double Now = GetWorld()->GetRealTimeSeconds();
	const float Margin = S(28.f);
	const float Top = S(28.f);

	if (!GameStateActor || !GameStateActor->GetMissionState().bActive)
	{
		// No job: a small waypoint card, or a hint.
		if (bHasWaypoint)
		{
			const FText Line = FText::Format(LOCTEXT("WaypointCard", "WAYPOINT  {0} m"), FText::AsNumber(FMath::RoundToInt(FVector::Dist2D(GetPlayerLocation(), Waypoint) / 100.f)));
			const FSlateFontInfo LineFont = Font(12.f, true);
			const float W = MeasureText(Line, LineFont).X + S(40.f);
			const float X = Canvas->ClipX - Margin - W;
			FillRect(X, Top, W, S(38.f), MishpachaHUDStyle::Panel);
			FillRect(X, Top, S(4.f), S(38.f), MishpachaHUDStyle::WaypointPurple);
			DrawLabel(Line, X + S(20.f), Top + S(10.f), LineFont, MishpachaHUDStyle::TextMain);
		}
		return;
	}

	const FReplicatedMissionState& Mission = GameStateActor->GetMissionState();
	const FLinearColor Accent = GetSiblingColor(Mission.RequiredSibling, MishpachaHUDStyle::Gold) * 1.35f;
	const float Appear = FMath::Clamp(float(Now - MissionShownTime) / 0.35f, 0.f, 1.f);
	const float Ease = 1.f - FMath::Square(1.f - Appear);

	const FSlateFontInfo TitleFont = Font(20.f, true);
	const FSlateFontInfo ObjectiveFont = Font(13.f);
	const FSlateFontInfo SmallFont = Font(9.5f, true);

	FText DistanceText;
	if (Mission.bHasGuide)
	{
		const float Meters = FVector::Dist2D(GetPlayerLocation(), Mission.GuideLocation) / 100.f;
		DistanceText = Meters >= 1000.f
			? FText::Format(LOCTEXT("DistanceKm", "{0} km"), FText::AsNumber(FMath::RoundToFloat(Meters / 100.f) / 10.f))
			: FText::Format(LOCTEXT("DistanceM", "{0} m"), FText::AsNumber(FMath::RoundToInt(Meters)));
	}

	const float TitleW = MeasureText(Mission.Title, TitleFont).X;
	const float ObjectiveW = MeasureText(Mission.ObjectiveText, ObjectiveFont).X + (DistanceText.IsEmpty() ? 0.f : MeasureText(DistanceText, Font(13.f, true)).X + S(24.f));
	const float W = FMath::Clamp(FMath::Max(TitleW, ObjectiveW) + S(64.f), S(400.f), S(680.f));
	const float H = S(132.f);
	const float X = Canvas->ClipX - Margin - W + (1.f - Ease) * S(60.f);
	const float Y = Top;
	const float Alpha = Ease;
	using MishpachaHUDStyle::WithAlpha;

	// Card.
	FillRect(X + S(4.f), Y + S(5.f), W, H, WithAlpha(FLinearColor::Black, 0.3f * Alpha));
	FillRect(X, Y, W, H, WithAlpha(MishpachaHUDStyle::Panel, Alpha));
	FillRect(X, Y, S(5.f), H, WithAlpha(Accent, Alpha));
	FillRect(X + S(5.f), Y, W - S(5.f), S(1.f), WithAlpha(MishpachaHUDStyle::PanelLight, Alpha));

	const float Left = X + S(24.f);
	const float Right = X + W - S(20.f);

	// Header: CURRENT JOB ............ RAFI
	DrawLabel(LOCTEXT("CurrentJob", "CURRENT JOB"), Left, Y + S(14.f), SmallFont, WithAlpha(Accent, Alpha));
	if (!Mission.RequiredSibling.IsNone())
	{
		DrawLabel(MishpachaText::SiblingName(Mission.RequiredSibling), Right, Y + S(14.f), SmallFont, WithAlpha(MishpachaHUDStyle::TextDim, Alpha), 1.f);
	}

	// Title.
	DrawLabel(Mission.Title, Left, Y + S(32.f), TitleFont, WithAlpha(MishpachaHUDStyle::TextMain, Alpha));

	// Divider.
	FillRect(Left, Y + S(72.f), W - S(44.f), S(1.f), WithAlpha(MishpachaHUDStyle::PanelLight, Alpha * 1.5f));

	// Objective row (flashes when it changes).
	const float ObjectiveY = Y + S(84.f);
	const float Flash = FMath::Clamp(1.f - float(Now - ObjectiveChangedTime) / 1.2f, 0.f, 1.f);
	if (Flash > 0.f)
	{
		FillRect(X + S(5.f), ObjectiveY - S(6.f), W - S(5.f), S(30.f), WithAlpha(Accent, 0.22f * Flash * Alpha));
	}
	DrawDiamond(FVector2D(Left + S(5.f), ObjectiveY + S(9.f)), S(5.f), WithAlpha(Accent, Alpha));
	DrawLabel(Mission.ObjectiveText, Left + S(18.f), ObjectiveY, ObjectiveFont, WithAlpha(MishpachaHUDStyle::TextMain, Alpha));
	if (!DistanceText.IsEmpty())
	{
		DrawLabel(DistanceText, Right, ObjectiveY, Font(13.f, true), WithAlpha(MishpachaHUDStyle::Gold, Alpha), 1.f);
	}

	// Steps: done / current / still to go.
	const float DotsY = Y + H - S(14.f);
	const float Pulse = 0.5f + 0.5f * FMath::Sin(float(Now) * 5.f);
	for (int32 Index = 0; Index < Mission.ObjectiveCount; ++Index)
	{
		const FVector2D P(Left + S(4.f) + Index * S(16.f), DotsY);
		if (Index < Mission.ObjectiveIndex)
		{
			DrawDisc(P, S(4.f), WithAlpha(Accent, Alpha), 12);
		}
		else if (Index == Mission.ObjectiveIndex)
		{
			DrawRing(P, S(6.f + Pulse * 1.5f), S(1.5f), WithAlpha(FLinearColor::White, (0.5f + 0.5f * Pulse) * Alpha), 16);
			DrawDisc(P, S(4.f), WithAlpha(FLinearColor::White, Alpha), 12);
		}
		else
		{
			DrawRing(P, S(4.f), S(1.5f), WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.35f), Alpha), 12);
		}
	}
	if (Mission.ObjectiveCount > 0)
	{
		DrawLabel(FText::Format(LOCTEXT("StepFmt", "STEP {0} / {1}"), FText::AsNumber(FMath::Max(1, Mission.ObjectiveIndex + 1)), FText::AsNumber(Mission.ObjectiveCount)),
			Right, DotsY - S(7.f), Font(8.5f, true), WithAlpha(MishpachaHUDStyle::TextDim, Alpha), 1.f);
	}
}

void AMishpachaHUD::DrawNotification()
{
	const double Age = GetWorld()->GetRealTimeSeconds() - LastNotificationTime;
	const double Duration = 4.5;
	if (LastNotification.IsEmpty() || Age > Duration)
	{
		return;
	}
	const float In = FMath::Clamp(float(Age) / 0.2f, 0.f, 1.f);
	const float Out = FMath::Clamp(float(Duration - Age) / 0.6f, 0.f, 1.f);
	const float Alpha = FMath::Min(In, Out);

	const FSlateFontInfo TextFont = Font(13.f, true);
	const FVector2D TextSize = MeasureText(LastNotification, TextFont);
	const float W = TextSize.X + S(48.f);
	const float H = S(42.f);
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = S(28.f) + (1.f - In) * S(-12.f);
	using MishpachaHUDStyle::WithAlpha;
	FillRect(X, Y, W, H, WithAlpha(MishpachaHUDStyle::Panel, Alpha));
	FillRect(X, Y + H - S(3.f), W, S(3.f), WithAlpha(MishpachaHUDStyle::Info, Alpha));
	DrawLabel(LastNotification, X + S(24.f), Y + (H - S(3.f) - TextSize.Y) * 0.5f, TextFont, WithAlpha(MishpachaHUDStyle::TextMain, Alpha));
}

void AMishpachaHUD::ShowPhoneLine(const FText& Caller, const FText& Line, float Seconds)
{
	PhoneCaller = Caller;
	PhoneText = Line;
	PhoneLineEnd = GetWorld()->GetRealTimeSeconds() + Seconds;
}

void AMishpachaHUD::DrawPhoneCall()
{
	if (GetWorld()->GetRealTimeSeconds() > PhoneLineEnd || PhoneText.IsEmpty())
	{
		return;
	}
	const FText Text = FText::Format(LOCTEXT("PhoneLine", "{0}: {1}"), PhoneCaller, PhoneText);
	const FSlateFontInfo TextFont = Font(13.f);
	const FVector2D TextSize = MeasureText(Text, TextFont);
	const float W = TextSize.X + S(44.f);
	const float H = S(40.f);
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY - S(204.f);
	FillRect(X, Y, W, H, FLinearColor(0.f, 0.f, 0.f, 0.65f));
	FillRect(X, Y, W, S(3.f), MishpachaHUDStyle::Info);
	DrawLabel(Text, X + S(22.f), Y + (H - TextSize.Y) * 0.5f, TextFont, FLinearColor::White);
}

void AMishpachaHUD::UpdateDistrict()
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	const ACityGreyboxGenerator* Gen = FindGenerator();
	if (!Gen || Now < NextDistrictCheck)
	{
		return;
	}
	NextDistrictCheck = Now + 1.0;
	const int32 District = static_cast<int32>(Gen->GetDistrictAt(GetPlayerLocation()));
	if (District != LastDistrict)
	{
		if (LastDistrict != INDEX_NONE)
		{
			ShowNotification(ACityGreyboxGenerator::GetDistrictName(static_cast<EMishpachaDistrict>(District)));
		}
		LastDistrict = District;
	}
}

void AMishpachaHUD::DrawBusinessInfo()
{
	using MishpachaHUDStyle::WithAlpha;
	const float CenterX = Canvas->ClipX * 0.5f;

	// A panel like the toast's, centered on the screen.
	auto Panel = [this, CenterX](const FText& Text, float Y, const FLinearColor& Accent)
	{
		const FSlateFontInfo TextFont = Font(12.f, true);
		const FVector2D TextSize = MeasureText(Text, TextFont);
		const float W = TextSize.X + S(40.f);
		const float H = S(36.f);
		FillRect(CenterX - W * 0.5f, Y, W, H, MishpachaHUDStyle::Panel);
		FillRect(CenterX - W * 0.5f, Y + H - S(3.f), W, S(3.f), Accent);
		DrawLabel(Text, CenterX, Y + (H - S(3.f) - TextSize.Y) * 0.5f, TextFont, MishpachaHUDStyle::TextMain, 0.5f);
	};

	// Under attack: the clock, wherever the player is.
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	for (TActorIterator<AMishpachaBusiness> It(GetWorld()); It; ++It)
	{
		if (It->GetState() == EMishpachaBusinessState::UnderAttack && GameState)
		{
			const int32 Left = FMath::Max(0, FMath::CeilToInt(It->GetAttackEndTime() - GameState->GetServerWorldTimeSeconds()));
			FNumberFormattingOptions TwoDigits;
			TwoDigits.MinimumIntegralDigits = 2;
			Panel(FText::Format(LOCTEXT("BusinessAttack", "{0} under attack  {1}:{2}"), It->GetDisplayName(), FText::AsNumber(Left / 60), FText::AsNumber(Left % 60, &TwoDigits)),
				S(78.f), FLinearColor(0.95f, 0.1f, 0.08f));
		}
	}

	// Standing at a business: what it is.
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	const AMishpachaBusiness* Near = PlayerCharacter ? AMishpachaBusiness::FindNearest(GetWorld(), PlayerCharacter->GetActorLocation(), AMishpachaBusiness::InteractRadius) : nullptr;
	if (!Near)
	{
		return;
	}
	const float Y = Canvas->ClipY - S(120.f);
	if (Near->GetState() == EMishpachaBusinessState::ForSale)
	{
		Panel(FText::Format(LOCTEXT("BusinessBuy", "F  Buy {0}: {1} NIS, pays {2} NIS"), Near->GetDisplayName(), FText::AsNumber(Near->GetCurrentPrice()), FText::AsNumber(Near->GetIncome())), Y, MishpachaHUDStyle::Gold);
	}
	else
	{
		Panel(FText::Format(LOCTEXT("BusinessOwned", "{0}: yours, pays {1} NIS"), Near->GetDisplayName(), FText::AsNumber(Near->GetIncome())), Y, MishpachaHUDStyle::Gold);
	}
}

void AMishpachaHUD::DrawBanner()
{
	const double Age = GetWorld()->GetRealTimeSeconds() - BannerTime;
	const double Duration = 3.5;
	if (Age > Duration || BannerTitle.IsEmpty())
	{
		return;
	}
	const float In = FMath::Clamp(float(Age) / 0.25f, 0.f, 1.f);
	const float Out = FMath::Clamp(float(Duration - Age) / 0.6f, 0.f, 1.f);
	const float Alpha = FMath::Min(In, Out);
	using MishpachaHUDStyle::WithAlpha;

	const float CY = Canvas->ClipY * 0.3f;
	const float StripH = S(120.f);
	FillRect(0.f, CY - StripH * 0.5f, Canvas->ClipX, StripH, WithAlpha(FLinearColor(0.f, 0.f, 0.f, 0.55f), Alpha));
	const float LineW = S(380.f) * (0.6f + 0.4f * In);
	FillRect((Canvas->ClipX - LineW) * 0.5f, CY + S(14.f), LineW, S(2.f), WithAlpha(BannerColor, Alpha));
	DrawLabel(BannerTitle, Canvas->ClipX * 0.5f, CY - S(46.f), Font(34.f, true), WithAlpha(BannerColor, Alpha), 0.5f);
	DrawLabel(BannerSubtitle, Canvas->ClipX * 0.5f, CY + S(22.f), Font(14.f), WithAlpha(MishpachaHUDStyle::TextMain, Alpha), 0.5f);
}

void AMishpachaHUD::DrawSubtitle()
{
	const AMishpachaGameState* GameStateActor = GetWorld()->GetGameState<AMishpachaGameState>();
	if (!GameStateActor || !GameStateActor->GetMissionState().bActive || GameStateActor->GetMissionState().ObjectiveDialogue.IsEmpty())
	{
		return;
	}
	const FReplicatedMissionState& Mission = GameStateActor->GetMissionState();
	const double Age = GetWorld()->GetRealTimeSeconds() - ObjectiveChangedTime;
	const double Duration = 7.0;
	if (Age < 0.4 || Age > Duration)
	{
		return; // a short beat after the objective flash, then the line
	}
	const float Alpha = FMath::Min(FMath::Clamp(float(Age - 0.4) / 0.25f, 0.f, 1.f), FMath::Clamp(float(Duration - Age) / 0.6f, 0.f, 1.f));
	using MishpachaHUDStyle::WithAlpha;

	const FText Who = FText::FromString(Mission.ObjectiveSpeaker.ToString().ToUpper());
	const FSlateFontInfo WhoFont = Font(12.f, true);
	const FSlateFontInfo LineFont = Font(15.f);
	const float WhoW = Who.IsEmpty() ? 0.f : MeasureText(Who, WhoFont).X + S(14.f);
	const FVector2D LineSize = MeasureText(Mission.ObjectiveDialogue, LineFont);
	const float W = WhoW + LineSize.X + S(44.f);
	const float H = S(44.f);
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY - S(150.f);
	const FLinearColor SpeakerColor = GetSiblingColor(FName(*Mission.ObjectiveSpeaker.ToString()), MishpachaHUDStyle::Gold) * 1.4f;

	FillRect(X, Y, W, H, WithAlpha(FLinearColor(0.f, 0.f, 0.f, 0.62f), Alpha));
	if (!Who.IsEmpty())
	{
		DrawLabel(Who, X + S(22.f), Y + (H - MeasureText(Who, WhoFont).Y) * 0.5f, WhoFont, WithAlpha(SpeakerColor, Alpha));
	}
	DrawLabel(Mission.ObjectiveDialogue, X + S(22.f) + WhoW, Y + (H - LineSize.Y) * 0.5f, LineFont, WithAlpha(MishpachaHUDStyle::TextMain, Alpha));
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

void AMishpachaHUD::SetMenuPage(EMishpachaMenuPage Page)
{
	if (Page != MenuPage)
	{
		MenuSelected = 0;
		bConfirmNewStory = false;
		MenuStatus = FText::GetEmpty();
		MenuOpenedTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	}
	MenuPage = Page;
}

void AMishpachaHUD::CloseMenu()
{
	if (AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(GetOwningPlayerController()))
	{
		PC->SetMenuOpen(false);
	}
	else
	{
		SetMenuPage(EMishpachaMenuPage::None);
	}
}

void AMishpachaHUD::ApplyGraphicsLevel(int32 Level)
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetOverallScalabilityLevel(Level);
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
}

void AMishpachaHUD::ApplyFrameLimit(float Limit)
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetFrameRateLimit(Limit);
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
}

void AMishpachaHUD::HandleSessionsFound(const TArray<FMishpachaSessionInfo>& Sessions, bool bSuccessful)
{
	bSearchingSessions = false;
	FoundSessions.Reset();
	for (const FMishpachaSessionInfo& Session : Sessions)
	{
		FoundSessions.Add(FText::Format(LOCTEXT("SessionFmt", "{0}  ({1}/{2})  {3} ms"),
			FText::FromString(Session.HostName), FText::AsNumber(Session.CurrentPlayers), FText::AsNumber(Session.MaxPlayers), FText::AsNumber(Session.PingMs)));
	}
	MenuStatus = bSuccessful ? FText::Format(LOCTEXT("SessionsFound", "{0} game(s) found."), FText::AsNumber(FoundSessions.Num())) : LOCTEXT("SessionsFailed", "Search failed.");
}

void AMishpachaHUD::BuildMenuItems(TArray<FMenuItem>& Items)
{
	UMishpachaGameInstance* GI = GetGameInstance<UMishpachaGameInstance>();
	UMishpachaSessionSubsystem* Sessions = GI ? GI->GetSessions() : nullptr;
	auto Item = [&Items](const FText& Label, const FText& Detail, TFunction<void()> Action, bool bEnabled = true)
	{
		FMenuItem& New = Items.AddDefaulted_GetRef();
		New.Label = Label;
		New.Detail = Detail;
		New.Action = MoveTemp(Action);
		New.bEnabled = bEnabled;
	};
	auto GoTo = [this](EMishpachaMenuPage Page) { return [this, Page]() { SetMenuPage(Page); }; };
	auto Quit = [this]() { UKismetSystemLibrary::QuitGame(this, GetOwningPlayerController(), EQuitPreference::Quit, false); };

	switch (MenuPage)
	{
	case EMishpachaMenuPage::Main:
	{
		const UMishpachaSaveGame* Save = GI ? GI->GetSave() : nullptr;
		const int32 JobsDone = Save ? Save->CompletedMissions.Num() : 0;
		Item(JobsDone > 0 ? LOCTEXT("MenuContinue", "CONTINUE") : LOCTEXT("MenuStart", "START STORY"),
			JobsDone > 0 ? FText::Format(LOCTEXT("MenuJobsDone", "{0} job(s) done"), FText::AsNumber(JobsDone)) : LOCTEXT("MenuStartDetail", "Rafi, Shira and Omer. Tel Avir is waiting."),
			[this]() { CloseMenu(); });
		Item(bConfirmNewStory ? LOCTEXT("MenuNewConfirm", "NEW STORY - SURE?") : LOCTEXT("MenuNew", "NEW STORY"),
			bConfirmNewStory ? LOCTEXT("MenuNewConfirmDetail", "Select again to erase all progress") : LOCTEXT("MenuNewDetail", "Start over from the first job"),
			[this, GI]()
			{
				if (!bConfirmNewStory)
				{
					bConfirmNewStory = true;
					return;
				}
				if (GI)
				{
					GI->ResetSave();
					GI->SaveToDisk();
				}
				UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)));
			});
		Item(LOCTEXT("MenuMultiplayer", "MULTIPLAYER"), LOCTEXT("MenuMultiplayerDetail", "Play with friends on your network"), GoTo(EMishpachaMenuPage::Multiplayer));
		Item(LOCTEXT("MenuSettings", "SETTINGS"), LOCTEXT("MenuSettingsDetail", "Graphics quality, frame rate"), GoTo(EMishpachaMenuPage::Settings));
		Item(LOCTEXT("MenuQuit", "QUIT"), FText::GetEmpty(), Quit);
		break;
	}
	case EMishpachaMenuPage::Pause:
		Item(LOCTEXT("MenuResume", "RESUME"), FText::GetEmpty(), [this]() { CloseMenu(); });
		Item(LOCTEXT("MenuSettings", "SETTINGS"), LOCTEXT("MenuSettingsDetail", "Graphics quality, frame rate"), GoTo(EMishpachaMenuPage::Settings));
		Item(LOCTEXT("MenuMainMenu", "MAIN MENU"), FText::GetEmpty(), GoTo(EMishpachaMenuPage::Main));
		Item(LOCTEXT("MenuQuit", "QUIT"), FText::GetEmpty(), Quit);
		break;
	case EMishpachaMenuPage::Multiplayer:
		Item(LOCTEXT("MenuHost", "HOST A GAME (LAN)"), LOCTEXT("MenuHostDetail", "Friends on the same network join you"), [this, Sessions]()
		{
			MenuStatus = (Sessions && Sessions->HostSession(8, true)) ? LOCTEXT("MenuHosting", "Starting the game...") : LOCTEXT("MenuHostFailed", "Couldn't host (online subsystem not available).");
		}, Sessions != nullptr);
		Item(LOCTEXT("MenuFind", "FIND GAMES (LAN)"), LOCTEXT("MenuFindDetail", "Look for games on your network"), [this, Sessions]()
		{
			FoundSessions.Reset();
			if (Sessions)
			{
				Sessions->OnFindSessionsComplete.AddUniqueDynamic(this, &AMishpachaHUD::HandleSessionsFound);
				bSearchingSessions = Sessions->FindSessions(20, true);
			}
			MenuStatus = bSearchingSessions ? LOCTEXT("MenuSearching", "Searching...") : LOCTEXT("MenuSearchFailed", "Couldn't search (online subsystem not available).");
			SetMenuPage(EMishpachaMenuPage::Sessions);
			if (bSearchingSessions)
			{
				MenuStatus = LOCTEXT("MenuSearching", "Searching...");
			}
		}, Sessions != nullptr);
		Item(LOCTEXT("MenuBack", "BACK"), FText::GetEmpty(), [this]() { MenuBack(); });
		break;
	case EMishpachaMenuPage::Sessions:
		for (int32 Index = 0; Index < FoundSessions.Num(); ++Index)
		{
			Item(FoundSessions[Index], LOCTEXT("MenuJoinDetail", "Join"), [this, Sessions, Index]()
			{
				MenuStatus = (Sessions && Sessions->JoinSessionByIndex(Index)) ? LOCTEXT("MenuJoining", "Joining...") : LOCTEXT("MenuJoinFailed", "Couldn't join.");
			});
		}
		if (FoundSessions.Num() == 0)
		{
			Item(bSearchingSessions ? LOCTEXT("MenuSearchingItem", "SEARCHING...") : LOCTEXT("MenuNoGames", "NO GAMES FOUND"), FText::GetEmpty(), []() {}, false);
		}
		Item(LOCTEXT("MenuRefresh", "SEARCH AGAIN"), FText::GetEmpty(), [this, Sessions]()
		{
			FoundSessions.Reset();
			bSearchingSessions = Sessions && Sessions->FindSessions(20, true);
			MenuStatus = bSearchingSessions ? LOCTEXT("MenuSearching", "Searching...") : LOCTEXT("MenuSearchFailed", "Couldn't search (online subsystem not available).");
		}, Sessions != nullptr);
		Item(LOCTEXT("MenuBack", "BACK"), FText::GetEmpty(), [this]() { MenuBack(); });
		break;
	case EMishpachaMenuPage::Settings:
	{
		const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
		const int32 Level = Settings ? Settings->GetOverallScalabilityLevel() : 1;
		const FText LevelNames[] = { LOCTEXT("QualityLow", "LOW"), LOCTEXT("QualityMedium", "MEDIUM"), LOCTEXT("QualityHigh", "HIGH"), LOCTEXT("QualityEpic", "EPIC") };
		const FText LevelName = (Level >= 0 && Level <= 3) ? LevelNames[Level] : LOCTEXT("QualityCustom", "CUSTOM");
		Item(FText::Format(LOCTEXT("MenuGraphics", "GRAPHICS: {0}"), LevelName), LOCTEXT("MenuGraphicsDetail", "Medium suits a GTX 1650 laptop"), [this, Level]()
		{
			ApplyGraphicsLevel((FMath::Max(Level, 0) + 1) % 4);
		});
		const float Limit = Settings ? Settings->GetFrameRateLimit() : 60.f;
		const FText LimitName = Limit <= 0.f ? LOCTEXT("FpsUnlimited", "UNLIMITED") : FText::AsNumber(FMath::RoundToInt(Limit));
		Item(FText::Format(LOCTEXT("MenuFps", "FRAME RATE LIMIT: {0}"), LimitName), LOCTEXT("MenuFpsDetail", "A limit keeps the laptop cooler"), [this, Limit]()
		{
			ApplyFrameLimit(Limit <= 0.f ? 30.f : (Limit < 45.f ? 60.f : 0.f));
		});
		const bool bHebrew = MishpachaText::GetLanguage() == TEXT("he");
		Item(FText::Format(LOCTEXT("MenuLanguage", "LANGUAGE: {0}"), bHebrew ? FText::FromString(TEXT("עברית")) : FText::FromString(TEXT("ENGLISH"))),
			MishpachaText::HasHebrewFont() ? LOCTEXT("MenuLanguageDetail", "English / Hebrew") : LOCTEXT("MenuLanguageNoFont", "Hebrew needs the Hebrew font (Tools/ImportFonts.py)"),
			[bHebrew]() { MishpachaText::SetLanguage(bHebrew ? TEXT("en") : TEXT("he")); }, MishpachaText::HasHebrewFont());
		Item(LOCTEXT("MenuBack", "BACK"), FText::GetEmpty(), [this]() { MenuBack(); });
		break;
	}
	default:
		break;
	}
}

void AMishpachaHUD::MenuNavigate(int32 Delta)
{
	TArray<FMenuItem> Items;
	BuildMenuItems(Items);
	if (Items.Num() == 0)
	{
		return;
	}
	MishpachaSounds::Play2D(this, TEXT("S_UI_Click"), 0.3f);
	for (int32 Step = 0; Step < Items.Num(); ++Step)
	{
		MenuSelected = (MenuSelected + Delta + Items.Num()) % Items.Num();
		if (Items[MenuSelected].bEnabled)
		{
			break;
		}
	}
}

void AMishpachaHUD::MenuActivate()
{
	TArray<FMenuItem> Items;
	BuildMenuItems(Items);
	if (Items.IsValidIndex(MenuSelected) && Items[MenuSelected].bEnabled && Items[MenuSelected].Action)
	{
		MishpachaSounds::Play2D(this, TEXT("S_UI_Click"), 0.45f, 0.75f);
		const bool bWasConfirming = bConfirmNewStory;
		Items[MenuSelected].Action();
		if (bWasConfirming && bConfirmNewStory && MenuPage == EMishpachaMenuPage::Main && MenuSelected != 1)
		{
			bConfirmNewStory = false;
		}
	}
}

void AMishpachaHUD::MenuBack()
{
	switch (MenuPage)
	{
	case EMishpachaMenuPage::Pause:
		CloseMenu();
		break;
	case EMishpachaMenuPage::Sessions:
		SetMenuPage(EMishpachaMenuPage::Multiplayer);
		break;
	case EMishpachaMenuPage::Settings:
	case EMishpachaMenuPage::Multiplayer:
		// Back to where we came from: the pause menu during play, otherwise the main menu.
		SetMenuPage(bMenuFromPause ? EMishpachaMenuPage::Pause : EMishpachaMenuPage::Main);
		break;
	default:
		break; // the main menu stays until you pick something
	}
}

void AMishpachaHUD::MenuClick(const FVector2D& ScreenPosition)
{
	for (int32 Index = 0; Index < MenuItemRects.Num(); ++Index)
	{
		if (MenuItemRects[Index].IsInside(ScreenPosition))
		{
			MenuSelected = Index;
			MenuActivate();
			return;
		}
	}
}

void AMishpachaHUD::DrawMenu()
{
	if (MenuPage == EMishpachaMenuPage::Pause)
	{
		bMenuFromPause = true;
	}
	else if (MenuPage == EMishpachaMenuPage::Main)
	{
		bMenuFromPause = false;
	}

	const float ClipX = Canvas->ClipX;
	const float ClipY = Canvas->ClipY;
	const double Now = GetWorld()->GetRealTimeSeconds();
	const float Appear = FMath::Clamp(float(Now - MenuOpenedTime) / 0.3f, 0.f, 1.f);
	using MishpachaHUDStyle::WithAlpha;

	// Backdrop: the (paused) city, darkened, with a panel on the left (on the right in Hebrew, mirrored).
	const bool bRTL = MishpachaText::IsRightToLeft();
	auto MirrorX = [bRTL, ClipX](float X) { return bRTL ? ClipX - X : X; };
	auto Inward = [bRTL](float D) { return bRTL ? -D : D; }; // an offset into the panel from its outer edge
	const float TextAlign = bRTL ? 1.f : 0.f;

	FillRect(0.f, 0.f, ClipX, ClipY, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	const float PanelW = FMath::Min(S(620.f), ClipX);
	FillRect(bRTL ? ClipX - PanelW : 0.f, 0.f, PanelW, ClipY, FLinearColor(0.008f, 0.01f, 0.016f, 0.82f));
	FillRect(bRTL ? ClipX - PanelW - S(2.f) : PanelW, 0.f, S(2.f), ClipY, WithAlpha(MishpachaHUDStyle::Gold, 0.5f));

	const float Left = MirrorX(S(84.f) - (1.f - Appear) * S(30.f)); // the text's start edge
	DrawLabel(LOCTEXT("MenuTitle", "MISHPACHA"), Left, S(110.f), Font(54.f, true), WithAlpha(MishpachaHUDStyle::Gold, Appear), TextAlign);
	DrawLabel(LOCTEXT("MenuTagline", "THE SELA FAMILY   /   TEL AVIR"), Left + Inward(S(4.f)), S(190.f), Font(11.f, true), WithAlpha(MishpachaHUDStyle::TextDim, Appear), TextAlign);

	FText PageTitle;
	switch (MenuPage)
	{
	case EMishpachaMenuPage::Pause: PageTitle = LOCTEXT("PagePaused", "PAUSED"); break;
	case EMishpachaMenuPage::Multiplayer: PageTitle = LOCTEXT("PageMultiplayer", "MULTIPLAYER"); break;
	case EMishpachaMenuPage::Sessions: PageTitle = LOCTEXT("PageSessions", "GAMES ON YOUR NETWORK"); break;
	case EMishpachaMenuPage::Settings: PageTitle = LOCTEXT("PageSettings", "SETTINGS"); break;
	default: break;
	}
	if (!PageTitle.IsEmpty())
	{
		DrawLabel(PageTitle, Left + Inward(S(4.f)), S(245.f), Font(13.f, true), WithAlpha(MishpachaHUDStyle::TextMain, Appear), TextAlign);
	}

	TArray<FMenuItem> Items;
	BuildMenuItems(Items);
	MenuSelected = FMath::Clamp(MenuSelected, 0, FMath::Max(0, Items.Num() - 1));

	// Mouse hover picks the item under the cursor (only when the mouse actually moved).
	float MouseX = 0.f;
	float MouseY = 0.f;
	const APlayerController* PC = GetOwningPlayerController();
	const bool bHasMouse = PC && PC->GetMousePosition(MouseX, MouseY);
	const FVector2D Mouse(MouseX, MouseY);
	const bool bMouseMoved = bHasMouse && FVector2D::DistSquared(Mouse, LastMenuMouse) > 4.0;
	LastMenuMouse = Mouse;

	MenuItemRects.Reset();
	float Y = S(300.f);
	const float ItemH = S(58.f);
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const float RectA = MirrorX(S(84.f) - S(24.f));
		const float RectB = MirrorX(PanelW - S(40.f));
		const FBox2D Rect(FVector2D(FMath::Min(RectA, RectB), Y), FVector2D(FMath::Max(RectA, RectB), Y + ItemH - S(6.f)));
		MenuItemRects.Add(Rect);
		if (bMouseMoved && Rect.IsInside(Mouse) && Items[Index].bEnabled)
		{
			MenuSelected = Index;
		}
		const bool bSelected = Index == MenuSelected && Items[Index].bEnabled;
		const float ItemAppear = FMath::Clamp(float(Now - MenuOpenedTime - 0.04 * Index) / 0.25f, 0.f, 1.f);
		if (bSelected)
		{
			FillRect(Rect.Min.X, Rect.Min.Y, Rect.GetSize().X, Rect.GetSize().Y, WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.07f), ItemAppear));
			FillRect(bRTL ? Rect.Max.X - S(4.f) : Rect.Min.X, Rect.Min.Y, S(4.f), Rect.GetSize().Y, WithAlpha(MishpachaHUDStyle::Gold, ItemAppear));
		}
		const FLinearColor LabelColor = !Items[Index].bEnabled ? FLinearColor(1.f, 1.f, 1.f, 0.28f) : (bSelected ? MishpachaHUDStyle::TextMain : MishpachaHUDStyle::TextDim);
		const float ItemX = Left + Inward(bSelected ? S(8.f) : 0.f);
		DrawLabel(Items[Index].Label, ItemX, Y + S(4.f), Font(19.f, true), WithAlpha(LabelColor, ItemAppear), TextAlign);
		if (!Items[Index].Detail.IsEmpty())
		{
			DrawLabel(Items[Index].Detail, ItemX, Y + S(32.f), Font(9.5f), WithAlpha(FLinearColor(1.f, 1.f, 1.f, bSelected ? 0.6f : 0.35f), ItemAppear), TextAlign);
		}
		Y += ItemH;
	}

	if (!MenuStatus.IsEmpty())
	{
		DrawLabel(MenuStatus, Left, Y + S(14.f), Font(11.f, true), MishpachaHUDStyle::Info, TextAlign);
	}

	float HintX = Left; // DrawKeyChip runs right to left from here in Hebrew
	const float HintY = ClipY - S(60.f);
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyEnter", "ENTER"), LOCTEXT("HintSelect", "Select"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyWS", "W / S"), LOCTEXT("HintMove", "Move"));
	if (MenuPage != EMishpachaMenuPage::Main)
	{
		DrawKeyChip(HintX, HintY, LOCTEXT("KeyP", "P"), LOCTEXT("HintBack", "Back"));
	}
}

void AMishpachaHUD::DrawKeyChip(float& X, float Y, const FText& Key, const FText& Label)
{
	const FSlateFontInfo KeyFont = Font(9.f, true);
	const FSlateFontInfo LabelFont = Font(10.f);
	const FVector2D KeySize = MeasureText(Key, KeyFont);
	const float ChipW = KeySize.X + S(12.f);
	const float ChipH = S(20.f);
	// Hebrew: X is the right edge and the chips run leftwards (key, then its label to the left of it).
	const bool bRTL = MishpachaText::IsRightToLeft();
	const float ChipX = bRTL ? X - ChipW : X;
	FillRect(ChipX, Y, ChipW, ChipH, FLinearColor(1.f, 1.f, 1.f, 0.14f));
	FillRect(ChipX, Y + ChipH - S(2.f), ChipW, S(2.f), FLinearColor(0.f, 0.f, 0.f, 0.35f));
	DrawLabel(Key, ChipX + S(6.f), Y + (ChipH - KeySize.Y) * 0.5f, KeyFont, MishpachaHUDStyle::TextMain, 0.f, false);
	const float LabelW = MeasureText(Label, LabelFont).X;
	if (bRTL)
	{
		X = ChipX - S(7.f);
		DrawLabel(Label, X, Y + S(2.f), LabelFont, FLinearColor(1.f, 1.f, 1.f, 0.7f), 1.f);
		X -= LabelW + S(20.f);
		return;
	}
	X += ChipW + S(7.f);
	DrawLabel(Label, X, Y + S(2.f), LabelFont, FLinearColor(1.f, 1.f, 1.f, 0.7f));
	X += LabelW + S(20.f);
}

void AMishpachaHUD::DrawControlsHint()
{
	float X = MishpachaText::IsRightToLeft() ? Canvas->ClipX - S(28.f) : S(28.f);
	const float Y = Canvas->ClipY - S(44.f);
	const APlayerController* PC = GetOwningPlayerController();
	if (PC && Cast<AMishpachaVehicle>(PC->GetPawn()))
	{
		DrawKeyChip(X, Y, LOCTEXT("KeyMMap", "M"), LOCTEXT("HintMap", "Map"));
		DrawKeyChip(X, Y, LOCTEXT("KeyF", "F"), LOCTEXT("HintExit", "Exit car"));
		DrawKeyChip(X, Y, LOCTEXT("KeySpace", "SPACE"), LOCTEXT("HintHandbrake", "Handbrake"));
		DrawKeyChip(X, Y, LOCTEXT("KeyR", "R"), LOCTEXT("HintRadio", "Radio"));
		DrawKeyChip(X, Y, LOCTEXT("KeyH", "H"), LOCTEXT("HintLights", "Lights"));
		return;
	}
	DrawKeyChip(X, Y, LOCTEXT("KeyTabWeapons", "TAB"), LOCTEXT("HintWeapons", "Weapons"));
	DrawKeyChip(X, Y, LOCTEXT("KeyMMap", "M"), LOCTEXT("HintMap", "Map"));
	DrawKeyChip(X, Y, LOCTEXT("Key123", "1 2 3"), LOCTEXT("HintSwitch", "Switch"));
	DrawKeyChip(X, Y, LOCTEXT("KeyQ", "Q"), LOCTEXT("HintAbility", "Ability"));
	DrawKeyChip(X, Y, LOCTEXT("KeyF", "F"), LOCTEXT("HintCar", "Car"));
	DrawKeyChip(X, Y, LOCTEXT("KeyShift", "SHIFT"), LOCTEXT("HintSprint", "Sprint"));
}

void AMishpachaHUD::DrawCrosshair()
{
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	const AMishpachaWeapon* Weapon = PlayerCharacter ? PlayerCharacter->GetWeapon() : nullptr;
	if (!Weapon || PlayerCharacter->GetCurrentVehicle() || PlayerCharacter->IsDead() || !PC->PlayerCameraManager)
	{
		return; // no gun in hand -> no crosshair
	}

	const double Now = GetWorld()->GetTimeSeconds();
	const FVector2D C(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);

	// Gap = the weapon's real spread cone on screen, plus bloom from moving and from the last shot.
	const float FOV = FMath::Max(PC->PlayerCameraManager->GetFOVAngle(), 10.f);
	const float HalfSpread = FMath::DegreesToRadians(Weapon->GetSpreadDegrees() * 0.5f);
	float Gap = FMath::Tan(HalfSpread) / FMath::Tan(FMath::DegreesToRadians(FOV * 0.5f)) * (Canvas->ClipX * 0.5f);
	const float Speed = PlayerCharacter->GetVelocity().Size2D();
	const float Kick = FMath::Clamp(1.f - float(Now - Weapon->GetLastShotTime()) / 0.15f, 0.f, 1.f);
	Gap = Gap * (PlayerCharacter->IsAiming() ? 0.8f : 1.f) + S(3.f) + Speed / 700.f * S(6.f) + Kick * S(7.f);

	// What's under the crosshair: police / rivals red, bystanders orange.
	FLinearColor Color(1.f, 1.f, 1.f, 0.95f);
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CrosshairTarget), false, PlayerCharacter);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * Weapon->GetRange(), ECC_Pawn, Params))
	{
		const AMishpachaCharacter* Target = Cast<AMishpachaCharacter>(Hit.GetActor());
		if (Target && !Target->IsDead())
		{
			const bool bHostile = Target->ActorHasTag(FName(TEXT("Police"))) || Target->ActorHasTag(FName(TEXT("Enemy")));
			Color = bHostile ? FLinearColor(1.f, 0.18f, 0.15f, 1.f) : FLinearColor(1.f, 0.65f, 0.2f, 1.f);
		}
	}

	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.55f);
	const float T = FMath::Max(1.5f, S(2.f));
	if (Weapon->GetPelletsPerShot() > 1)
	{
		// Shotgun: a ring showing the pellet spread.
		const float Radius = FMath::Max(Gap, S(14.f));
		DrawRing(C, Radius + 1.f, T + 2.f, Shadow, 40);
		DrawRing(C, Radius, T, Color, 40);
	}
	else
	{
		const float Len = S(Weapon->IsAutomatic() ? 9.f : 7.f);
		auto Ticks = [&](float Thickness, const FLinearColor& TickColor, float Extra)
		{
			FillRect(C.X - Gap - Len - Extra, C.Y - Thickness * 0.5f, Len + Extra * 2.f, Thickness, TickColor);
			FillRect(C.X + Gap - Extra, C.Y - Thickness * 0.5f, Len + Extra * 2.f, Thickness, TickColor);
			FillRect(C.X - Thickness * 0.5f, C.Y - Gap - Len - Extra, Thickness, Len + Extra * 2.f, TickColor);
			FillRect(C.X - Thickness * 0.5f, C.Y + Gap - Extra, Thickness, Len + Extra * 2.f, TickColor);
		};
		Ticks(T + 2.f, Shadow, 1.f);
		Ticks(T, Color, 0.f);
	}
	DrawDisc(C, S(2.6f), Shadow, 10);
	DrawDisc(C, S(1.7f), Color, 10);

	// Hit marker: an X when the server confirms a hit (red on a kill).
	const float HitAge = float(Now - Weapon->GetLastHitTime());
	if (HitAge < 0.25f)
	{
		const float A = 1.f - HitAge / 0.25f;
		const FLinearColor MarkColor = Weapon->WasLastHitKill() ? FLinearColor(1.f, 0.15f, 0.1f, A) : FLinearColor(1.f, 1.f, 1.f, A);
		const float In = Gap + S(4.f);
		const float Out = In + S(9.f);
		for (const FVector2D& D : { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1) })
		{
			const FVector2D Dir = D.GetSafeNormal();
			DrawThickLine(C + Dir * In, C + Dir * Out, S(2.5f), MarkColor);
		}
	}

	// Ammo next to the crosshair.
	const FText AmmoText = Weapon->IsReloading()
		? LOCTEXT("Reloading", "RELOADING")
		: FText::Format(LOCTEXT("AmmoFmt", "{0} / {1}"), FText::AsNumber(Weapon->GetAmmo()), FText::AsNumber(Weapon->GetMagazineSize()));
	const bool bLow = !Weapon->IsReloading() && Weapon->GetAmmo() <= FMath::Max(1, Weapon->GetMagazineSize() / 5);
	const float AmmoOffset = FMath::Max(Gap + S(16.f), S(30.f)) * 0.75f; // just outside the reticle, down-right
	DrawLabel(AmmoText, C.X + AmmoOffset + S(6.f), C.Y + AmmoOffset, Font(10.f, true), bLow || Weapon->IsReloading() ? MishpachaHUDStyle::Gold : FLinearColor(1.f, 1.f, 1.f, 0.8f));
}

// ---------------------------------------------------------------------------
// Weapon wheel
// ---------------------------------------------------------------------------

void AMishpachaHUD::OpenWeaponWheel()
{
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	bWheelOpen = true;
	WheelOpenedTime = GetWorld()->GetRealTimeSeconds();
	WheelSelected = PlayerCharacter ? PlayerCharacter->GetEquippedSlot() + 1 : 0; // segment 0 = unarmed
}

int32 AMishpachaHUD::CloseWeaponWheel()
{
	bWheelOpen = false;
	return WheelSelected - 1;
}

void AMishpachaHUD::DrawWeaponIcon(const FVector2D& Center, int32 LoadoutSlot, float Size, const FLinearColor& Color)
{
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	const UClass* WeaponClass = (PlayerCharacter && PlayerCharacter->GetLoadout().IsValidIndex(LoadoutSlot)) ? PlayerCharacter->GetLoadout()[LoadoutSlot].Get() : nullptr;
	if (!WeaponClass)
	{
		// Fist.
		DrawDisc(Center, Size * 0.42f, Color, 20);
		FillRect(Center.X - Size * 0.42f, Center.Y - Size * 0.1f, Size * 0.84f, Size * 0.08f, FLinearColor(0.f, 0.f, 0.f, 0.5f));
		return;
	}
	// Side-view silhouettes from boxes: barrel/body length and grip differ per type.
	float BodyLength = 0.7f;
	float BodyHeight = 0.26f;
	float BarrelLength = 0.25f;
	bool bStock = false;
	bool bMagazine = false;
	if (WeaponClass->IsChildOf(AMishpachaSMG::StaticClass())) { BodyLength = 0.85f; BarrelLength = 0.2f; bMagazine = true; }
	else if (WeaponClass->IsChildOf(AMishpachaShotgun::StaticClass())) { BodyLength = 1.3f; BodyHeight = 0.2f; BarrelLength = 0.45f; bStock = true; }
	else if (WeaponClass->IsChildOf(AMishpachaRifle::StaticClass())) { BodyLength = 1.25f; BarrelLength = 0.4f; bStock = true; bMagazine = true; }
	const float W = Size * BodyLength;
	const float H = Size * BodyHeight;
	const float X = Center.X - W * 0.5f;
	const float Y = Center.Y - H;
	FillRect(X, Y, W, H, Color);                                                   // receiver
	FillRect(X + W, Y + H * 0.15f, Size * BarrelLength, H * 0.4f, Color);           // barrel
	FillRect(X + W * (bStock ? 0.22f : 0.1f), Y + H, W * 0.16f, Size * 0.32f, Color); // grip
	if (bMagazine)
	{
		FillRect(X + W * 0.48f, Y + H, W * 0.12f, Size * 0.36f, Color);
	}
	if (bStock)
	{
		FillRect(X - Size * 0.32f, Y + H * 0.1f, Size * 0.32f, H * 1.1f, Color);
	}
}

void AMishpachaHUD::DrawWeaponWheel()
{
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	if (!PlayerCharacter)
	{
		return;
	}
	const TArray<TSubclassOf<AMishpachaWeapon>>& Loadout = PlayerCharacter->GetLoadout();
	const int32 Count = Loadout.Num() + 1; // + unarmed
	const FVector2D C(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float Inner = S(95.f);
	const float Outer = S(235.f);
	const float Appear = FMath::Clamp(float(GetWorld()->GetRealTimeSeconds() - WheelOpenedTime) / 0.12f, 0.f, 1.f);
	using MishpachaHUDStyle::WithAlpha;

	// Pick the segment the cursor points at (ignore tiny movements around the center).
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (PC->GetMousePosition(MouseX, MouseY))
	{
		const FVector2D Delta = FVector2D(MouseX, MouseY) - C;
		if (Delta.Size() > S(40.f))
		{
			// Segment 0 is centered at the top; angles go clockwise on screen.
			float Angle = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) + 90.f;
			Angle = FMath::Fmod(Angle + 360.f + 180.f / Count, 360.f);
			WheelSelected = FMath::Clamp(FMath::FloorToInt(Angle / (360.f / Count)), 0, Count - 1);
		}
	}

	FillRect(0.f, 0.f, Canvas->ClipX, Canvas->ClipY, FLinearColor(0.f, 0.f, 0.f, 0.35f * Appear));

	const float Span = 360.f / Count;
	const float GapDeg = 2.5f;
	for (int32 Segment = 0; Segment < Count; ++Segment)
	{
		const bool bSelected = Segment == WheelSelected;
		const float MidDeg = -90.f + Segment * Span;
		const float R1 = Inner;
		const float R2 = (Outer + (bSelected ? S(14.f) : 0.f)) * (0.85f + 0.15f * Appear);
		TArray<FCanvasUVTri> Tris;
		const int32 Steps = 16;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float A0 = FMath::DegreesToRadians(MidDeg - Span * 0.5f + GapDeg + (Span - 2.f * GapDeg) * Step / Steps);
			const float A1 = FMath::DegreesToRadians(MidDeg - Span * 0.5f + GapDeg + (Span - 2.f * GapDeg) * (Step + 1) / Steps);
			const FVector2D D0(FMath::Cos(A0), FMath::Sin(A0));
			const FVector2D D1(FMath::Cos(A1), FMath::Sin(A1));
			AddQuad(Tris, C + D0 * R1, C + D0 * R2, C + D1 * R2, C + D1 * R1,
				bSelected ? WithAlpha(FLinearColor(0.95f, 0.72f, 0.15f, 0.88f), Appear) : WithAlpha(FLinearColor(0.02f, 0.025f, 0.035f, 0.78f), Appear));
		}
		DrawTris(Tris);

		// Icon + name in the middle of the segment.
		const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(MidDeg)), FMath::Sin(FMath::DegreesToRadians(MidDeg)));
		const FVector2D Mid = C + Dir * ((R1 + R2) * 0.5f);
		const FLinearColor IconColor = bSelected ? FLinearColor(0.03f, 0.03f, 0.03f, Appear) : WithAlpha(MishpachaHUDStyle::TextMain, Appear);
		DrawWeaponIcon(Mid - FVector2D(0.f, S(8.f)), Segment - 1, S(46.f), IconColor);
		const AMishpachaWeapon* Defaults = Segment > 0 && Loadout[Segment - 1] ? Loadout[Segment - 1]->GetDefaultObject<AMishpachaWeapon>() : nullptr;
		const FText Name = Defaults ? Defaults->GetDisplayName() : LOCTEXT("Unarmed", "Unarmed");
		DrawLabel(FText::FromString(Name.ToString().ToUpper()), Mid.X, Mid.Y + S(22.f), Font(9.f, true), IconColor, 0.5f, !bSelected);
	}

	// Center: the picked weapon's stats.
	DrawDisc(C, Inner - S(8.f), WithAlpha(FLinearColor(0.01f, 0.012f, 0.02f, 0.85f), Appear), 40);
	const AMishpachaWeapon* Picked = WheelSelected > 0 && Loadout.IsValidIndex(WheelSelected - 1) && Loadout[WheelSelected - 1] ? Loadout[WheelSelected - 1]->GetDefaultObject<AMishpachaWeapon>() : nullptr;
	DrawLabel(Picked ? FText::FromString(Picked->GetDisplayName().ToString().ToUpper()) : LOCTEXT("UnarmedUpper", "UNARMED"),
		C.X, C.Y - S(48.f), Font(12.f, true), WithAlpha(MishpachaHUDStyle::Gold, Appear), 0.5f);
	if (Picked)
	{
		auto Stat = [&](const FText& Label, float Value, float Y)
		{
			DrawLabel(Label, C.X - S(58.f), Y - S(6.f), Font(7.5f, true), WithAlpha(MishpachaHUDStyle::TextDim, Appear));
			FillRect(C.X - S(8.f), Y, S(64.f), S(4.f), WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.15f), Appear));
			FillRect(C.X - S(8.f), Y, S(64.f) * FMath::Clamp(Value, 0.05f, 1.f), S(4.f), WithAlpha(MishpachaHUDStyle::TextMain, Appear));
		};
		Stat(LOCTEXT("StatDamage", "DAMAGE"), Picked->GetDamage() / 120.f, C.Y - S(16.f));
		Stat(LOCTEXT("StatRate", "FIRE RATE"), Picked->GetFireRate() / 12.f, C.Y + S(2.f));
		Stat(LOCTEXT("StatRange", "RANGE"), Picked->GetRange() / 15000.f, C.Y + S(20.f));
		DrawLabel(FText::Format(LOCTEXT("StatMag", "MAG {0}"), FText::AsNumber(Picked->GetMagazineSize())), C.X, C.Y + S(34.f), Font(8.f, true), WithAlpha(MishpachaHUDStyle::TextDim, Appear), 0.5f);
	}
	DrawLabel(LOCTEXT("WheelHint", "Release TAB to equip"), C.X, C.Y + Outer + S(30.f), Font(10.f, true), WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.7f), Appear), 0.5f);
}

void AMishpachaHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !GetWorld())
	{
		return;
	}
	UIScale = FMath::Clamp(Canvas->ClipY / 1080.f, 0.6f, 2.5f);

	EnsureMapTexture();
	TrackMissionState();

	if (IsMenuOpen())
	{
		DrawMenu();
		return;
	}

	if (bMapOpen)
	{
		DrawFullMap();
		DrawNotification();
		return;
	}

	if (bDrawMinimap)
	{
		DrawMinimap();
	}

	if (HUDWidget || !bDrawDebugOverlay)
	{
		return;
	}
	DrawMissionCard();
	DrawBanner();
	DrawSubtitle();
	DrawPhoneCall();
	DrawBusinessInfo();
	UpdateDistrict();
	DrawNotification();
	DrawControlsHint();
	if (bWheelOpen)
	{
		DrawWeaponWheel();
	}
	else
	{
		DrawCrosshair();
	}
}

#undef LOCTEXT_NAMESPACE
