// MISHPACHA - lightweight global gameplay events.
#include "World/MishpachaWorldEvents.h"

FMishpachaGunshotSignature& FMishpachaWorldEvents::OnGunshot()
{
	static FMishpachaGunshotSignature Delegate;
	return Delegate;
}
