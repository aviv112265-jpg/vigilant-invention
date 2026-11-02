// MISHPACHA - loads the synthesized sounds (Tools/CreateSounds.py) and plays them with sensible falloff.
#pragma once

#include "CoreMinimal.h"

class USoundBase;
class USoundAttenuation;
class UAudioComponent;
class USceneComponent;
class UObject;

/** Everything is silent (never an error) when /Game/Mishpacha/Audio hasn't been generated. */
namespace MishpachaSounds
{
	/** "S_Shot_Pistol" -> /Game/Mishpacha/Audio/S_Shot_Pistol (cached). */
	MISHPACHA_API USoundBase* Get(FName Name);

	/** Shared distance falloff: sounds fade to silence at FalloffDistance (cm). */
	MISHPACHA_API USoundAttenuation* Attenuation(float FalloffDistance);

	MISHPACHA_API void PlayAt(const UObject* WorldContext, FName Name, const FVector& Location, float Volume = 1.f, float Pitch = 1.f, float FalloffDistance = 6000.f);
	MISHPACHA_API void Play2D(const UObject* WorldContext, FName Name, float Volume = 1.f, float Pitch = 1.f);

	/** A looping sound on a component (not started). Null when the sound is missing. */
	MISHPACHA_API UAudioComponent* MakeLoop(USceneComponent* AttachTo, FName Name, float FalloffDistance, bool bSpatial = true);
}
