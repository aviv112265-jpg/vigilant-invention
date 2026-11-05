// MISHPACHA - side characters phone the family in free roam, depending on how far the story got.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MishpachaPhoneSubsystem.generated.h"

/** One call: who, when it unlocks, and its lines. Each call plays once (remembered in the save). */
struct FMishpachaPhoneCall
{
	FName Id;
	FName RequiredMission;
	bool bRequiresBusiness = false;
	FText Caller;
	TArray<FText> Lines;
};

/** Server only, story mode. Waits for a quiet moment: no job, no police, nobody shooting. */
UCLASS()
class MISHPACHA_API UMishpachaPhoneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Starts the clock (the city calls this in story mode). */
	void Start();

	/** Ring now, if a call is available. */
	bool TryCall();

	UPROPERTY(EditDefaultsOnly, Category = "Phone")
	FVector2D CallDelay = FVector2D(150.f, 300.f);

	UPROPERTY(EditDefaultsOnly, Category = "Phone")
	float SecondsPerLine = 5.5f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	TArray<FMishpachaPhoneCall> Calls;
	FTimerHandle CallTimer;
	FTimerHandle LineTimer;
	int32 CallIndex = INDEX_NONE;
	int32 LineIndex = 0;

	void BuildCalls();
	void Schedule(float Delay);
	void OnTimer();
	void NextLine();
	class AMishpachaPlayerController* GetPlayer() const;
};
