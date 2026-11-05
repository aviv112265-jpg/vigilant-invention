// MISHPACHA - HUD actor: creates the UMG widget and feeds it game events.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Fonts/SlateFontInfo.h"
#include "Missions/MissionTypes.h"
#include "Online/MishpachaSessionSubsystem.h" // FMishpachaSessionInfo (menu session list)
#include "MishpachaHUD.generated.h"

class UMishpachaHUDWidget;
class AMishpachaPlayerState;
class AMishpachaCharacter;
class ACityGreyboxGenerator;
class AMishpachaRouteGuide;
class UTexture2D;
class UFont;
struct FCanvasUVTri;

UENUM()
enum class EMishpachaMenuPage : uint8
{
	None,
	Main,
	Pause,
	Multiplayer,
	Sessions,
	Settings
};

/**
 * If HUDWidgetClass is set (BP child of this HUD), creates the widget and forwards events to it.
 * Without a widget, draws the built-in canvas HUD: job card, status panel, toasts and banners.
 *
 * Always (widget or not): the minimap (top left), the full map (Tab / M) with a player waypoint,
 * and the GPS route painted on the road towards the current objective or the waypoint.
 */
UCLASS()
class MISHPACHA_API AMishpachaHUD : public AHUD
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "HUD")
	UMishpachaHUDWidget* GetHUDWidget() const { return HUDWidget; }

	void ShowNotification(const FText& Message);
	void ShowPhoneLine(const FText& Caller, const FText& Line, float Seconds);
	void SetSwitchMenuOpen(bool bOpen);

	// ----- Map (driven by AMishpachaPlayerController) -----
	void SetMapOpen(bool bOpen);
	bool IsMapOpen() const { return bMapOpen; }
	void HandleMapClick(const FVector2D& ScreenPosition);
	void PanMap(const FVector2D& Axis, float DeltaSeconds);
	void ZoomMap(float Steps);

	UFUNCTION(BlueprintCallable, Category = "HUD|Map")
	void SetWaypoint(const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "HUD|Map")
	void ClearWaypoint();

	UFUNCTION(BlueprintPure, Category = "HUD|Map")
	bool HasWaypoint() const { return bHasWaypoint; }

	// ----- Weapon wheel (hold Tab): the cursor's direction from the screen center picks a slot -----
	void OpenWeaponWheel();
	/** Returns the picked loadout slot (INDEX_NONE = unarmed). */
	int32 CloseWeaponWheel();
	bool IsWeaponWheelOpen() const { return bWheelOpen; }

	// ----- Menus (main menu at start, pause menu on P / Esc); input comes from the player controller -----
	void SetMenuPage(EMishpachaMenuPage Page);
	EMishpachaMenuPage GetMenuPage() const { return MenuPage; }
	bool IsMenuOpen() const { return MenuPage != EMishpachaMenuPage::None; }
	void MenuNavigate(int32 Delta);
	void MenuActivate();
	void MenuBack();
	void MenuClick(const FVector2D& ScreenPosition);

	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	TSubclassOf<UMishpachaHUDWidget> HUDWidgetClass;

	/** Built-in canvas HUD when there's no widget. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD")
	bool bDrawDebugOverlay = true;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Map")
	bool bDrawMinimap = true;

	/** Show the main menu when a single-player game starts. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Menu")
	bool bShowMainMenuOnStart = true;

	/** Paint the GPS route on the road (chevrons). The route is always drawn on the maps. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Map")
	bool bShowRouteOnRoad = true;

	/** World width (cm) the minimap shows on foot / in a vehicle. */
	UPROPERTY(EditDefaultsOnly, Category = "HUD|Map")
	float MinimapRangeOnFoot = 9000.f;

	UPROPERTY(EditDefaultsOnly, Category = "HUD|Map")
	float MinimapRangeInVehicle = 16000.f;

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

	UFUNCTION()
	void HandleSessionsFound(const TArray<FMishpachaSessionInfo>& Sessions, bool bSuccessful);

private:
	// ----- Weapon wheel -----
	bool bWheelOpen = false;
	int32 WheelSelected = 0;   // index into the wheel's segments (0 = unarmed)
	double WheelOpenedTime = 0.0;
	void DrawWeaponWheel();
	void DrawWeaponIcon(const FVector2D& Center, int32 LoadoutSlot, float Size, const FLinearColor& Color);

	// ----- Menus -----
	struct FMenuItem
	{
		FText Label;
		FText Detail;
		TFunction<void()> Action;
		bool bEnabled = true;
	};
	EMishpachaMenuPage MenuPage = EMishpachaMenuPage::None;
	int32 MenuSelected = 0;
	bool bConfirmNewStory = false;
	double MenuOpenedTime = 0.0;
	TArray<FBox2D> MenuItemRects;
	TArray<FText> FoundSessions;
	bool bSearchingSessions = false;
	bool bMenuFromPause = false;
	FVector2D LastMenuMouse = FVector2D::ZeroVector;
	FText MenuStatus;

	void BuildMenuItems(TArray<FMenuItem>& OutItems);
	void DrawMenu();
	void CloseMenu();
	void ApplyGraphicsLevel(int32 Level);
	void ApplyFrameLimit(float Limit);

	TWeakObjectPtr<AMishpachaPlayerState> BoundPlayerState;
	TWeakObjectPtr<AMishpachaCharacter> BoundCharacter;
	bool bBoundGameState = false;
	bool bBoundFamily = false;
	FTimerHandle BindTimer;

	void RefreshBindings();
	void StartAutomatedCapture();

	// ----- Toasts / banners -----
	FText LastNotification;
	double LastNotificationTime = -1000.0;

	FText PhoneCaller;
	FText PhoneText;
	double PhoneLineEnd = -1000.0;

	int32 LastDistrict = INDEX_NONE;
	double NextDistrictCheck = 0.0;
	FText BannerTitle;
	FText BannerSubtitle;
	FLinearColor BannerColor = FLinearColor::White;
	double BannerTime = -1000.0;

	// ----- Job card animation -----
	FName ShownMissionId;
	int32 ShownObjectiveIndex = INDEX_NONE;
	double MissionShownTime = -1000.0;
	double ObjectiveChangedTime = -1000.0;

	// ----- Map -----
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MapTexture;

	UPROPERTY(Transient)
	TObjectPtr<UFont> UIFont;

	UPROPERTY(Transient)
	TObjectPtr<AMishpachaRouteGuide> RouteGuide;

	TWeakObjectPtr<ACityGreyboxGenerator> Generator;
	FBox2D MapBounds = FBox2D(ForceInit);

	bool bMapOpen = false;
	FVector2D MapViewCenter = FVector2D::ZeroVector;
	float MapZoom = 1.f;

	/** Where the full map was last drawn (screen) and which world rectangle it showed. */
	FVector2D FullMapScreenMin = FVector2D::ZeroVector;
	FVector2D FullMapScreenSize = FVector2D::ZeroVector;
	FVector2D FullMapWorldMin = FVector2D::ZeroVector;
	FVector2D FullMapWorldSize = FVector2D(1.0, 1.0);

	bool bHasWaypoint = false;
	FVector Waypoint = FVector::ZeroVector;

	/** Current GPS route (world), starting at the player. */
	TArray<FVector> RoutePoints;
	FLinearColor RouteColor = FLinearColor::Yellow;
	bool bRouteIsMission = false;
	FTimerHandle RouteTimer;

	/** UI scale for the current frame (1 at 1080p). */
	float UIScale = 1.f;

	ACityGreyboxGenerator* FindGenerator();
	void EnsureMapTexture();
	void UpdateRoute();
	void TrackMissionState();
	FBox2D GetEffectiveMapBounds() const;
	FLinearColor GetSiblingColor(FName SiblingId, const FLinearColor& Fallback) const;
	FVector GetPlayerLocation(float* OutYaw = nullptr) const;
	void ClampMapView();

	// ----- Drawing -----
	struct FMapView
	{
		FVector2D ScreenMin;
		FVector2D ScreenSize;
		FVector2D WorldMin;
		FVector2D WorldSize;

		FVector2D ToScreen(const FVector& World) const;
		FVector ToWorld(const FVector2D& Screen) const;
		bool Contains(const FVector2D& Screen, float Inset = 0.f) const;
		FVector2D ClampToEdge(const FVector2D& Screen, float Inset) const;
	};

	void DrawMinimap();
	void DrawFullMap();
	void DrawMapContents(const FMapView& View, bool bFullMap);
	void DrawMapLayer(const FMapView& View);
	void DrawStatusPanel(float X, float Y, float Width);
	void DrawMissionCard();
	void DrawNotification();
	void DrawBanner();
	void DrawSubtitle();
	void DrawBusinessInfo();
	void DrawPhoneCall();
	void UpdateDistrict();
	void DrawControlsHint();
	void DrawCrosshair();

	float S(float Value) const { return Value * UIScale; }
	FSlateFontInfo Font(float Size, bool bBold = false) const;
	FVector2D MeasureText(const FText& Text, const FSlateFontInfo& FontInfo) const;
	void DrawLabel(const FText& Text, float X, float Y, const FSlateFontInfo& FontInfo, const FLinearColor& Color, float HAlign = 0.f, bool bShadow = true);
	void FillRect(float X, float Y, float W, float H, const FLinearColor& Color);
	void DrawTris(const TArray<FCanvasUVTri>& Tris);
	static void AddTri(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);
	static void AddQuad(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Color);
	void DrawDisc(const FVector2D& Center, float Radius, const FLinearColor& Color, int32 Segments = 20);
	void DrawRing(const FVector2D& Center, float Radius, float Thickness, const FLinearColor& Color, int32 Segments = 24);
	void DrawThickLine(const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color);
	void DrawPlayerArrow(const FVector2D& Center, float Yaw, float Size, const FLinearColor& Color);
	void DrawStar(const FVector2D& Center, float Radius, const FLinearColor& Color);
	void DrawDiamond(const FVector2D& Center, float Radius, const FLinearColor& Color);
	void DrawPolylineClipped(const TArray<FVector>& WorldPoints, const FMapView& View, float Thickness, const FLinearColor& Color);
	void DrawKeyChip(float& X, float Y, const FText& Key, const FText& Label);
};
