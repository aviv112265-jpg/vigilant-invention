// MISHPACHA - day / night cycle: moves the level's sun, drives lit windows and street lamps.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MishpachaDayNight.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class UMaterialParameterCollection;

/**
 * Time of day comes from the server clock (GameState server world time), so every machine
 * shows the same hour without replicating it every frame.
 *
 * Works on any level: it adopts the first directional light (made Movable) as the sun, and at night
 * turns it into a dim blue moon. The scalar "Night" (0 day .. 1 night) in MPC_Mishpacha lights the
 * windows of M_Facade and the street lamps (M_LampGlow, M_Asphalt / M_Sidewalk light pools).
 *
 * Debug: -MishpachaHour=21.5 on the command line starts at that hour.
 */
UCLASS()
class MISHPACHA_API AMishpachaDayNight : public AActor
{
	GENERATED_BODY()

public:
	AMishpachaDayNight();

	/** Hours, 0..24. */
	UFUNCTION(BlueprintPure, Category = "Time")
	float GetTimeOfDay() const;

	/** 0 = full day, 1 = full night. */
	UFUNCTION(BlueprintPure, Category = "Time")
	float GetNightAmount() const { return NightAmount; }

	/** Server: jump to an hour (0..24). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Time")
	void SetTimeOfDay(float Hour);

	static AMishpachaDayNight* Find(const UObject* WorldContextObject);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Hour at server time 0. */
	UPROPERTY(EditAnywhere, Replicated, Category = "Time", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHour = 10.f;

	/** Real minutes per in-game day (0 = time stands still). */
	UPROPERTY(EditAnywhere, Replicated, Category = "Time", meta = (ClampMin = "0"))
	float MinutesPerDay = 24.f;

	/** Compass direction of the sun's path (yaw of the sunrise). */
	UPROPERTY(EditAnywhere, Category = "Sun")
	float SunPathYaw = 35.f;

	UPROPERTY(EditAnywhere, Category = "Sun")
	float MoonIntensityScale = 0.05f;

	UPROPERTY(EditAnywhere, Category = "Sun")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.65f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Sun")
	FLinearColor SunsetColor = FLinearColor(1.f, 0.55f, 0.3f);

	/** Exposure compensation at full night (stops; negative = darker). */
	UPROPERTY(EditAnywhere, Category = "Sun")
	float NightExposureBias = -3.f;

private:
	TWeakObjectPtr<class APostProcessVolume> Exposure;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> Parameters;

	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<USkyLightComponent> SkyLight;
	float DayIntensity = 0.f;
	float DaySkyIntensity = 1.f;
	FLinearColor DayColor = FLinearColor::White;
	float NightAmount = 0.f;
	float LastCapturedNight = -1.f;

	void FindLights();
};
