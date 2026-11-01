// MISHPACHA - day / night cycle.
#include "World/MishpachaDayNight.h"
#include "Mishpacha.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"

AMishpachaDayNight::AMishpachaDayNight()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetHidden(true);
}

void AMishpachaDayNight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaDayNight, StartHour);
	DOREPLIFETIME(AMishpachaDayNight, MinutesPerDay);
}

AMishpachaDayNight* AMishpachaDayNight::Find(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AMishpachaDayNight> It(World);
	return It ? *It : nullptr;
}

void AMishpachaDayNight::BeginPlay()
{
	Super::BeginPlay();

	Parameters = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/Game/Mishpacha/Materials/MPC_Mishpacha.MPC_Mishpacha"), nullptr, LOAD_Quiet | LOAD_NoWarn);

	float DebugHour = 0.f;
	if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("MishpachaHour="), DebugHour))
	{
		SetTimeOfDay(DebugHour);
	}
	FindLights();

	// Our own unbound post-process volume, only for the night exposure (local to each machine).
	FActorSpawnParameters Params;
	Params.Owner = this;
	if (APostProcessVolume* Volume = GetWorld()->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, Params))
	{
		Volume->bUnbound = true;
		Volume->Priority = 10.f;
		Volume->Settings.bOverride_AutoExposureBias = true;
		Volume->Settings.AutoExposureBias = 0.f;
		Exposure = Volume;
	}
}

void AMishpachaDayNight::FindLights()
{
	UWorld* World = GetWorld();
	if (!Sun.IsValid())
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
			{
				Light->SetMobility(EComponentMobility::Movable);
				Sun = Light;
				DayIntensity = Light->Intensity;
				DayColor = Light->GetLightColor();
				break;
			}
		}
	}
	if (!SkyLight.IsValid())
	{
		for (TActorIterator<ASkyLight> It(World); It; ++It)
		{
			if (USkyLightComponent* Light = It->GetLightComponent())
			{
				Light->SetMobility(EComponentMobility::Movable);
				SkyLight = Light;
				DaySkyIntensity = Light->Intensity;
				break;
			}
		}
	}
}

float AMishpachaDayNight::GetTimeOfDay() const
{
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const double Seconds = GameState ? GameState->GetServerWorldTimeSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	const double Hours = MinutesPerDay > 0.f ? Seconds / (MinutesPerDay * 60.0) * 24.0 : 0.0;
	return float(FMath::Fmod(StartHour + Hours, 24.0));
}

void AMishpachaDayNight::SetTimeOfDay(float Hour)
{
	if (!HasAuthority())
	{
		return;
	}
	// Re-base the clock so that "now" is Hour.
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const double Seconds = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const double Elapsed = MinutesPerDay > 0.f ? Seconds / (MinutesPerDay * 60.0) * 24.0 : 0.0;
	StartHour = float(FMath::Fmod(FMath::Fmod(double(Hour) - Elapsed, 24.0) + 24.0, 24.0));
	ForceNetUpdate();
}

void AMishpachaDayNight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Sun.IsValid() || !SkyLight.IsValid())
	{
		FindLights(); // the gray-box city may spawn its sun after us
	}

	// Sun elevation: sunrise at 6:00, noon overhead-ish, sunset at 18:00.
	const float Hour = GetTimeOfDay();
	const float DayAngle = (Hour - 6.f) / 24.f * 360.f; // 0 sunrise, 90 noon, 180 sunset, 270 midnight
	const float Elevation = FMath::Sin(FMath::DegreesToRadians(DayAngle)); // -1..1
	NightAmount = FMath::Clamp((0.08f - Elevation) / 0.25f, 0.f, 1.f);

	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		const bool bMoon = Elevation < 0.f;
		// The moon rides the same arc half a day later; it lights the ground, not the atmosphere (dark night sky).
		const float Angle = bMoon ? DayAngle - 180.f : DayAngle;
		Light->SetWorldRotation(FRotator(-Angle, SunPathYaw, 0.f));
		if (Light->IsUsedAsAtmosphereSunLight() == bMoon)
		{
			Light->SetAtmosphereSunLight(!bMoon);
		}
		if (bMoon)
		{
			Light->SetIntensity(DayIntensity * MoonIntensityScale * FMath::Clamp(-Elevation * 4.f, 0.f, 1.f));
			Light->SetLightColor(MoonColor);
		}
		else
		{
			Light->SetIntensity(DayIntensity * FMath::Clamp(Elevation * 4.f, 0.f, 1.f));
			Light->SetLightColor(FMath::Lerp(SunsetColor, DayColor, FMath::Clamp(Elevation * 2.5f, 0.f, 1.f)));
		}
	}

	// A sky light that doesn't capture every frame needs a fresh capture as the sky changes.
	if (USkyLightComponent* Sky = SkyLight.Get())
	{
		if (!Sky->bRealTimeCapture && FMath::Abs(NightAmount - LastCapturedNight) > 0.05f)
		{
			LastCapturedNight = NightAmount;
			Sky->RecaptureSky();
		}
		Sky->SetIntensity(DaySkyIntensity * FMath::Lerp(1.f, 0.25f, NightAmount));
	}

	if (Parameters)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, Parameters, FName(TEXT("Night")), NightAmount);
	}

	// Auto exposure would brighten the night back to day: pull the exposure down as it gets dark.
	if (APostProcessVolume* Volume = Exposure.Get())
	{
		Volume->Settings.AutoExposureBias = FMath::Lerp(0.f, NightExposureBias, NightAmount);
	}
}
