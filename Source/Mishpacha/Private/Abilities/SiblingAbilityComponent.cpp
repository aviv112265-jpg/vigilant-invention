// MISHPACHA - base class for each sibling's special ability.
#include "Abilities/SiblingAbilityComponent.h"
#include "Characters/MishpachaCharacter.h"
#include "Core/MishpachaPlayerController.h"
#include "Heat/CrimeLibrary.h"
#include "Heat/HeatComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "MishpachaAbility"

USiblingAbilityComponent::USiblingAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	AbilityName = LOCTEXT("DefaultAbilityName", "Ability");
}

void USiblingAbilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USiblingAbilityComponent, bAbilityActive);
	DOREPLIFETIME(USiblingAbilityComponent, LastActivationTime);
}

void USiblingAbilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EndAbility();
	}
	Super::EndPlay(EndPlayReason);
}

void USiblingAbilityComponent::TryActivate()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		HandleActivateRequest();
	}
	else
	{
		ServerTryActivate();
	}
}

void USiblingAbilityComponent::ServerTryActivate_Implementation()
{
	HandleActivateRequest();
}

void USiblingAbilityComponent::HandleActivateRequest()
{
	if (bAbilityActive)
	{
		return;
	}

	FText Reason;
	if (!CanActivate(Reason))
	{
		NotifyOwner(Reason);
		return;
	}

	bAbilityActive = true;
	LastActivationTime = GetServerTime();
	ActivateAbility();

	// The ability may have ended itself during activation (e.g. failed to spawn something).
	if (!bAbilityActive)
	{
		return;
	}

	OnRep_AbilityActive();

	if (Duration > 0.f)
	{
		GetWorld()->GetTimerManager().SetTimer(DurationTimer, this, &USiblingAbilityComponent::EndAbility, Duration, false);
	}
	else
	{
		EndAbility();
	}
}

bool USiblingAbilityComponent::CanActivate(FText& OutReason) const
{
	const AMishpachaCharacter* OwnerCharacter = Cast<AMishpachaCharacter>(GetOwner());
	if (OwnerCharacter && OwnerCharacter->IsDead())
	{
		OutReason = LOCTEXT("AbilityDead", "Not while you're down.");
		return false;
	}

	const float Remaining = GetCooldownRemaining();
	if (Remaining > 0.f)
	{
		OutReason = FText::Format(LOCTEXT("AbilityCooldown", "{0} is ready in {1} seconds."), AbilityName, FText::AsNumber(FMath::CeilToInt(Remaining)));
		return false;
	}
	return true;
}

void USiblingAbilityComponent::EndAbility()
{
	if (!bAbilityActive)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DurationTimer);
	}

	bAbilityActive = false;
	DeactivateAbility();
	OnRep_AbilityActive();
}

void USiblingAbilityComponent::ForceEnd()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EndAbility();
	}
}

float USiblingAbilityComponent::GetCooldownRemaining() const
{
	return FMath::Max(0.f, static_cast<float>(LastActivationTime + Cooldown - GetServerTime()));
}

void USiblingAbilityComponent::OnRep_AbilityActive()
{
	if (bAbilityActive)
	{
		BP_OnActivated();
	}
	else
	{
		BP_OnDeactivated();
	}
}

APawn* USiblingAbilityComponent::GetOwnerPawn() const
{
	return Cast<APawn>(GetOwner());
}

AController* USiblingAbilityComponent::GetOwnerController() const
{
	const APawn* OwnerPawn = GetOwnerPawn();
	return OwnerPawn ? OwnerPawn->GetController() : nullptr;
}

UHeatComponent* USiblingAbilityComponent::GetOwnerHeat() const
{
	return UCrimeLibrary::FindHeatComponent(GetOwner());
}

double USiblingAbilityComponent::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return World->GetTimeSeconds();
}

void USiblingAbilityComponent::NotifyOwner(const FText& Message) const
{
	if (Message.IsEmpty())
	{
		return;
	}
	if (AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(GetOwnerController()))
	{
		PC->ClientShowNotification(Message);
	}
}

#undef LOCTEXT_NAMESPACE
