// MISHPACHA - HUD actor.
#include "UI/MishpachaHUD.h"
#include "UI/MishpachaHUDWidget.h"
#include "Core/MishpachaGameState.h"
#include "Core/MishpachaPlayerState.h"
#include "Characters/MishpachaCharacter.h"
#include "Family/FamilySubsystem.h"
#include "Heat/HeatComponent.h"
#include "Abilities/SiblingAbilityComponent.h"
#include "Engine/Canvas.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"

void AMishpachaHUD::BeginPlay()
{
	Super::BeginPlay();

	if (HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UMishpachaHUDWidget>(GetOwningPlayerController(), HUDWidgetClass);
		if (HUDWidget)
		{
			HUDWidget->AddToViewport();
		}
	}

	// PlayerState / pawn / game state arrive at different times on clients - keep (re)binding.
	GetWorldTimerManager().SetTimer(BindTimer, this, &AMishpachaHUD::RefreshBindings, 0.5f, true, 0.f);
}

void AMishpachaHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindTimer);
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
	LastNotification = Message;
	LastNotificationTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (HUDWidget)
	{
		HUDWidget->OnNotification(Message);
	}
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

void AMishpachaHUD::DrawHUD()
{
	Super::DrawHUD();

	if (HUDWidget || !bDrawDebugOverlay)
	{
		return;
	}

	// Minimal gray-box overlay (English only: the default debug font has no Hebrew glyphs).
	float CursorY = 40.f;
	auto Line = [this, &CursorY](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, Color, 40.f, CursorY);
		CursorY += 22.f;
	};

	const APlayerController* PC = GetOwningPlayerController();
	const AMishpachaPlayerState* PS = PC ? PC->GetPlayerState<AMishpachaPlayerState>() : nullptr;
	if (PS)
	{
		Line(FString::Printf(TEXT("Money: NIS %d"), PS->GetMoney()), FLinearColor::Green);
		if (const UHeatComponent* Heat = PS->GetHeatComponent())
		{
			const int32 Level = Heat->GetHeatLevel();
			FString HeatLine = FString::Printf(TEXT("Heat: %d / 5"), Level);
			if (Level > 0)
			{
				HeatLine += FString::Printf(TEXT("  (escape %d%%)"), FMath::RoundToInt(Heat->GetEscapeProgress() * 100.f));
			}
			Line(HeatLine, Level > 0 ? FLinearColor::Red : FLinearColor::White);
		}
	}

	if (const AMishpachaCharacter* PawnCharacter = PC ? Cast<AMishpachaCharacter>(PC->GetPawn()) : nullptr)
	{
		const FString Who = PawnCharacter->GetSiblingId().IsNone() ? TEXT("Player") : PawnCharacter->GetSiblingId().ToString();
		Line(FString::Printf(TEXT("%s  HP %.0f / %.0f"), *Who, PawnCharacter->GetHealth(), PawnCharacter->GetMaxHealth()), PawnCharacter->GetPlaceholderColor());

		if (const USiblingAbilityComponent* Ability = PawnCharacter->GetAbility())
		{
			const float Cooldown = Ability->GetCooldownRemaining();
			FString AbilityLine = FString::Printf(TEXT("[Q] %s: "), *Ability->GetAbilityName().ToString());
			AbilityLine += Ability->IsAbilityActive() ? TEXT("ACTIVE") : (Cooldown > 0.f ? FString::Printf(TEXT("%.0fs"), Cooldown) : FString(TEXT("ready")));
			Line(AbilityLine, FLinearColor(0.8f, 0.8f, 1.f));
		}

		// Simple crosshair while on foot.
		if (Canvas && !PawnCharacter->GetCurrentVehicle() && !PawnCharacter->IsDead())
		{
			const float CX = Canvas->ClipX * 0.5f;
			const float CY = Canvas->ClipY * 0.5f;
			DrawLine(CX - 8.f, CY, CX + 8.f, CY, FLinearColor::White, 1.5f);
			DrawLine(CX, CY - 8.f, CX, CY + 8.f, FLinearColor::White, 1.5f);
		}
	}

	if (const AMishpachaGameState* GameStateActor = GetWorld() ? GetWorld()->GetGameState<AMishpachaGameState>() : nullptr)
	{
		const FReplicatedMissionState& Mission = GameStateActor->GetMissionState();
		if (Mission.bActive)
		{
			Line(FString::Printf(TEXT("Job: %s"), *Mission.Title.ToString()), FLinearColor::Yellow);
			Line(FString::Printf(TEXT("> %s"), *Mission.ObjectiveText.ToString()), FLinearColor::Yellow);
		}
	}

	if (!LastNotification.IsEmpty() && GetWorld() && GetWorld()->GetTimeSeconds() - LastNotificationTime < 5.0)
	{
		Line(LastNotification.ToString(), FLinearColor(0.6f, 0.85f, 1.f));
	}

	Line(TEXT("[WASD] move  [Shift] sprint  [F] enter/exit car  [LMB/RMB] shoot/aim  [Q] ability  [1/2/3] switch sibling"), FLinearColor::Gray);
}
