// MISHPACHA - sound helpers.
#include "World/MishpachaSounds.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

namespace MishpachaSounds
{
	// Rooted raw pointers: static TStrongObjectPtrs would be released after UObject shutdown at exit.
	static TMap<FName, USoundBase*>& Cache()
	{
		static TMap<FName, USoundBase*> Sounds;
		return Sounds;
	}

	USoundBase* Get(FName Name)
	{
		if (USoundBase** Found = Cache().Find(Name))
		{
			return *Found;
		}
		const FString Path = FString::Printf(TEXT("/Game/Mishpacha/Audio/%s.%s"), *Name.ToString(), *Name.ToString());
		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (Sound)
		{
			Sound->AddToRoot();
		}
		Cache().Add(Name, Sound);
		return Sound;
	}

	USoundAttenuation* Attenuation(float FalloffDistance)
	{
		static TMap<int32, USoundAttenuation*> Attenuations;
		const int32 Key = FMath::RoundToInt(FalloffDistance);
		if (USoundAttenuation** Found = Attenuations.Find(Key))
		{
			return *Found;
		}
		USoundAttenuation* Settings = NewObject<USoundAttenuation>(GetTransientPackage());
		Settings->Attenuation.bAttenuate = true;
		Settings->Attenuation.bSpatialize = true;
		Settings->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Settings->Attenuation.AttenuationShapeExtents = FVector(FalloffDistance * 0.08f, 0.f, 0.f); // full volume this close
		Settings->Attenuation.FalloffDistance = FalloffDistance;
		Settings->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Settings->AddToRoot();
		Attenuations.Add(Key, Settings);
		return Settings;
	}

	void PlayAt(const UObject* WorldContext, FName Name, const FVector& Location, float Volume, float Pitch, float FalloffDistance)
	{
		if (USoundBase* Sound = Get(Name))
		{
			UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, Volume, Pitch, 0.f, Attenuation(FalloffDistance));
		}
	}

	void Play2D(const UObject* WorldContext, FName Name, float Volume, float Pitch)
	{
		if (USoundBase* Sound = Get(Name))
		{
			UGameplayStatics::PlaySound2D(WorldContext, Sound, Volume, Pitch);
		}
	}

	UAudioComponent* MakeLoop(USceneComponent* AttachTo, FName Name, float FalloffDistance, bool bSpatial)
	{
		USoundBase* Sound = Get(Name);
		if (!Sound || !AttachTo)
		{
			return nullptr;
		}
		UAudioComponent* Audio = NewObject<UAudioComponent>(AttachTo->GetOwner());
		Audio->SetSound(Sound);
		Audio->bAutoActivate = false;
		Audio->bAllowSpatialization = bSpatial;
		if (bSpatial)
		{
			Audio->AttenuationSettings = Attenuation(FalloffDistance);
		}
		Audio->SetupAttachment(AttachTo);
		Audio->RegisterComponent();
		return Audio;
	}
}
