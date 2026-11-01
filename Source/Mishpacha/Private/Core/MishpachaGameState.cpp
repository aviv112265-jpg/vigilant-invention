// MISHPACHA - replicated world state.
#include "Core/MishpachaGameState.h"
#include "Net/UnrealNetwork.h"

void AMishpachaGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaGameState, MissionState);
}

void AMishpachaGameState::SetMissionState(const FReplicatedMissionState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}
	MissionState = NewState;
	OnRep_MissionState();
	ForceNetUpdate();
}

void AMishpachaGameState::OnRep_MissionState()
{
	OnMissionStateChanged.Broadcast(MissionState);
}
