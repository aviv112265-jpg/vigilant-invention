// MISHPACHA - Shira's ability: "Connections".
#include "Abilities/ShiraConnectionsAbility.h"
#include "Heat/HeatComponent.h"

#define LOCTEXT_NAMESPACE "MishpachaAbility"

UShiraConnectionsAbility::UShiraConnectionsAbility()
{
	AbilityName = LOCTEXT("ShiraAbilityName", "Connections");
	Cooldown = 180.f;
	Duration = 0.f;
}

bool UShiraConnectionsAbility::CanActivate(FText& OutReason) const
{
	if (!Super::CanActivate(OutReason))
	{
		return false;
	}

	const UHeatComponent* Heat = GetOwnerHeat();
	if (!Heat || Heat->GetHeatLevel() <= 0)
	{
		OutReason = LOCTEXT("ShiraNoHeat", "Nobody is looking for you. Save the favor.");
		return false;
	}
	if (Heat->GetHeatLevel() >= UnusableAtHeatLevel)
	{
		OutReason = LOCTEXT("ShiraTooHot", "Too hot. Nobody is picking up the phone right now.");
		return false;
	}
	return true;
}

void UShiraConnectionsAbility::ActivateAbility()
{
	if (UHeatComponent* Heat = GetOwnerHeat())
	{
		Heat->ReduceHeatLevels(LevelsToReduce);
		NotifyOwner(LOCTEXT("ShiraCalled", "Shira made a call. The patrols are being redirected."));
	}
}

#undef LOCTEXT_NAMESPACE
