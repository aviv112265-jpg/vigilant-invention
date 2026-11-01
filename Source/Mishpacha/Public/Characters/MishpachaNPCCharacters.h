// MISHPACHA - ready-to-use NPC characters (work without any Blueprint or asset setup).
#pragma once

#include "CoreMinimal.h"
#include "Characters/MishpachaCharacter.h"
#include "MishpachaNPCCharacters.generated.h"

/** Police officer: tagged "Police", carries a pistol, driven by AMishpachaPoliceAIController. */
UCLASS()
class MISHPACHA_API AMishpachaPolicePawn : public AMishpachaCharacter
{
	GENERATED_BODY()

public:
	AMishpachaPolicePawn();
};

/** Pedestrian: tagged "Civilian" (a potential witness), unarmed, wanders and flees from gunfire. */
UCLASS()
class MISHPACHA_API AMishpachaCivilian : public AMishpachaCharacter
{
	GENERATED_BODY()

public:
	AMishpachaCivilian();
};

/** Sela family soldier summoned by Rafi's ability. Follows Rafi (the ability issues the move orders). */
UCLASS()
class MISHPACHA_API AMishpachaFamilySoldier : public AMishpachaCharacter
{
	GENERATED_BODY()

public:
	AMishpachaFamilySoldier();
};
