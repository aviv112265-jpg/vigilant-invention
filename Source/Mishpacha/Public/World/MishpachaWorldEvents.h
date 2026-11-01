// MISHPACHA - lightweight global gameplay events (server-side, non-reflected).
#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Broadcast on the server whenever a gun is fired: (World, Location of the shot). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FMishpachaGunshotSignature, UWorld* /*World*/, const FVector& /*Location*/);

struct MISHPACHA_API FMishpachaWorldEvents
{
	/** Civilians listen to this to flee from gunfire. */
	static FMishpachaGunshotSignature& OnGunshot();
};
