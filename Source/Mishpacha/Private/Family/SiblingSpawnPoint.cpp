// MISHPACHA - marks a sibling's "home" start location in the level.
#include "Family/SiblingSpawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"

ASiblingSpawnPoint::ASiblingSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowSize = 2.f;
	Arrow->SetHiddenInGame(true);
}
