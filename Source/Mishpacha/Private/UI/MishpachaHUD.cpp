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
#include "World/MishpachaDayNight.h"
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
#include "Misc/CommandLine.h"

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

	if (FParse::Param(FCommandLine::Get(), TEXT("MishpachaCapture")))
	{
		StartAutomatedCapture();
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
	auto After = [this](float Delay, const FTimerDelegate& Delegate)
	{
		FTimerHandle Handle; // one handle per timer: reusing a handle would cancel the previous one
		GetWorldTimerManager().SetTimer(Handle, Delegate, Delay, false);
	};
	After(15.f, Command(TEXT("startfpschart")));
	After(40.f, Command(TEXT("stopfpschart")));
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
	return FSlateFontInfo(UIFont.Get(), FMath::Max(1, FMath::RoundToInt(Size * UIScale)), bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular")));
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

void AMishpachaHUD::DrawLabel(const FText& Text, float X, float Y, const FSlateFontInfo& FontInfo, const FLinearColor& Color, float HAlign, bool bShadow)
{
	if (!Canvas || Text.IsEmpty() || !UIFont)
	{
		return;
	}
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
				DrawLabel(FText::FromName(AsCharacter->GetSiblingId()), P.X + Blip * 1.5f, P.Y - S(8.f), Font(10.f, true), AsCharacter->GetPlaceholderColor());
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
	DrawLabel(LOCTEXT("MapKeyHint", "TAB  MAP"), X + Size - S(8.f), Y + Size - S(20.f), Font(8.5f, true), FLinearColor(1.f, 1.f, 1.f, 0.55f), 1.f);

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
	float HintX = MapX;
	const float HintY = MapY + Side + S(18.f);
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyLMB", "LMB"), LOCTEXT("HintSet", "Set / remove waypoint"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyRMB", "RMB"), LOCTEXT("HintClear", "Clear waypoint"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyWheel", "WHEEL"), LOCTEXT("HintZoom", "Zoom"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyWASD", "WASD"), LOCTEXT("HintPan", "Move map"));
	DrawKeyChip(HintX, HintY, LOCTEXT("KeyTab", "TAB"), LOCTEXT("HintClose", "Close"));
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
		? FText::FromString(PlayerCharacter->GetSiblingId().ToString().ToUpper())
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
		DrawLabel(FText::FromString(Mission.RequiredSibling.ToString().ToUpper()), Right, Y + S(14.f), SmallFont, WithAlpha(MishpachaHUDStyle::TextDim, Alpha), 1.f);
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

void AMishpachaHUD::DrawKeyChip(float& X, float Y, const FText& Key, const FText& Label)
{
	const FSlateFontInfo KeyFont = Font(9.f, true);
	const FSlateFontInfo LabelFont = Font(10.f);
	const FVector2D KeySize = MeasureText(Key, KeyFont);
	const float ChipW = KeySize.X + S(12.f);
	const float ChipH = S(20.f);
	FillRect(X, Y, ChipW, ChipH, FLinearColor(1.f, 1.f, 1.f, 0.14f));
	FillRect(X, Y + ChipH - S(2.f), ChipW, S(2.f), FLinearColor(0.f, 0.f, 0.f, 0.35f));
	DrawLabel(Key, X + S(6.f), Y + (ChipH - KeySize.Y) * 0.5f, KeyFont, MishpachaHUDStyle::TextMain, 0.f, false);
	X += ChipW + S(7.f);
	DrawLabel(Label, X, Y + S(2.f), LabelFont, FLinearColor(1.f, 1.f, 1.f, 0.7f));
	X += MeasureText(Label, LabelFont).X + S(20.f);
}

void AMishpachaHUD::DrawControlsHint()
{
	float X = S(28.f);
	const float Y = Canvas->ClipY - S(44.f);
	const APlayerController* PC = GetOwningPlayerController();
	if (PC && Cast<AMishpachaVehicle>(PC->GetPawn()))
	{
		DrawKeyChip(X, Y, LOCTEXT("KeyTabMap", "TAB"), LOCTEXT("HintMap", "Map"));
		DrawKeyChip(X, Y, LOCTEXT("KeyF", "F"), LOCTEXT("HintExit", "Exit car"));
		DrawKeyChip(X, Y, LOCTEXT("KeySpace", "SPACE"), LOCTEXT("HintHandbrake", "Handbrake"));
		DrawKeyChip(X, Y, LOCTEXT("KeyR", "R"), LOCTEXT("HintRadio", "Radio"));
		return;
	}
	DrawKeyChip(X, Y, LOCTEXT("KeyTabMap", "TAB"), LOCTEXT("HintMap", "Map"));
	DrawKeyChip(X, Y, LOCTEXT("Key123", "1 2 3"), LOCTEXT("HintSwitch", "Switch"));
	DrawKeyChip(X, Y, LOCTEXT("KeyQ", "Q"), LOCTEXT("HintAbility", "Ability"));
	DrawKeyChip(X, Y, LOCTEXT("KeyF", "F"), LOCTEXT("HintCar", "Car"));
	DrawKeyChip(X, Y, LOCTEXT("KeyShift", "SHIFT"), LOCTEXT("HintSprint", "Sprint"));
}

void AMishpachaHUD::DrawCrosshair()
{
	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaCharacter* PlayerCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr;
	if (!PlayerCharacter || PlayerCharacter->GetCurrentVehicle() || PlayerCharacter->IsDead())
	{
		return;
	}
	const FVector2D C(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	const float Gap = S(5.f);
	const float Len = S(7.f);
	const float T = FMath::Max(1.f, S(2.f));
	auto Ticks = [&](float Thickness, const FLinearColor& Color, float Extra)
	{
		FillRect(C.X - Gap - Len - Extra, C.Y - Thickness * 0.5f, Len + Extra * 2.f, Thickness, Color);
		FillRect(C.X + Gap - Extra, C.Y - Thickness * 0.5f, Len + Extra * 2.f, Thickness, Color);
		FillRect(C.X - Thickness * 0.5f, C.Y - Gap - Len - Extra, Thickness, Len + Extra * 2.f, Color);
		FillRect(C.X - Thickness * 0.5f, C.Y + Gap - Extra, Thickness, Len + Extra * 2.f, Color);
	};
	Ticks(T + 2.f, FLinearColor(0.f, 0.f, 0.f, 0.5f), 1.f);
	Ticks(T, FLinearColor(1.f, 1.f, 1.f, 0.9f), 0.f);
	DrawDisc(C, S(1.6f), FLinearColor(1.f, 1.f, 1.f, 0.9f), 8);
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
	DrawNotification();
	DrawControlsHint();
	DrawCrosshair();
}

#undef LOCTEXT_NAMESPACE
