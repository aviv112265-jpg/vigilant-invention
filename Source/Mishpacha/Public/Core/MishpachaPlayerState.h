// MISHPACHA - per-player replicated state: money (NIS) and Heat.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "MishpachaPlayerState.generated.h"

class UHeatComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMishpachaMoneyChanged, int32, NewMoney);

UCLASS()
class MISHPACHA_API AMishpachaPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AMishpachaPlayerState();

	UFUNCTION(BlueprintPure, Category = "Economy")
	int32 GetMoney() const { return Money; }

	/** Server only. In Story mode this mirrors the ACTIVE sibling's wallet. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Economy")
	void SetMoney(int32 NewMoney);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Economy")
	void AddMoney(int32 Delta);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Economy")
	bool TrySpendMoney(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Heat")
	UHeatComponent* GetHeatComponent() const { return HeatComponent; }

	UPROPERTY(BlueprintAssignable, Category = "Economy")
	FOnMishpachaMoneyChanged OnMoneyChanged;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Heat")
	TObjectPtr<UHeatComponent> HeatComponent;

	UPROPERTY(ReplicatedUsing = OnRep_Money)
	int32 Money = 0;

	UFUNCTION()
	void OnRep_Money();
};
