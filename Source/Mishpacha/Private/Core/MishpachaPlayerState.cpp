// MISHPACHA - per-player replicated state.
#include "Core/MishpachaPlayerState.h"
#include "Heat/HeatComponent.h"
#include "Net/UnrealNetwork.h"

AMishpachaPlayerState::AMishpachaPlayerState()
{
	HeatComponent = CreateDefaultSubobject<UHeatComponent>(TEXT("HeatComponent"));
}

void AMishpachaPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaPlayerState, Money);
}

void AMishpachaPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (AMishpachaPlayerState* Target = Cast<AMishpachaPlayerState>(PlayerState))
	{
		Target->Money = Money;
	}
}

void AMishpachaPlayerState::SetMoney(int32 NewMoney)
{
	if (!HasAuthority())
	{
		return;
	}
	Money = FMath::Max(0, NewMoney);
	OnRep_Money();
	ForceNetUpdate();
}

void AMishpachaPlayerState::AddMoney(int32 Delta)
{
	SetMoney(Money + Delta);
}

bool AMishpachaPlayerState::TrySpendMoney(int32 Amount)
{
	if (!HasAuthority() || Amount < 0 || Money < Amount)
	{
		return false;
	}
	SetMoney(Money - Amount);
	return true;
}

void AMishpachaPlayerState::OnRep_Money()
{
	OnMoneyChanged.Broadcast(Money);
}
