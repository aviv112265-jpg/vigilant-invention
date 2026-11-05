// MISHPACHA - story save data.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MishpachaSaveGame.generated.h"

USTRUCT(BlueprintType)
struct FSiblingSaveData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Save")
	bool bHasTransform = false;

	UPROPERTY(BlueprintReadWrite, Category = "Save")
	FTransform Transform;

	UPROPERTY(BlueprintReadWrite, Category = "Save")
	int32 Money = 0;
};

UCLASS()
class MISHPACHA_API UMishpachaSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bump when the layout changes so old saves can be migrated. */
	UPROPERTY(BlueprintReadWrite, Category = "Save")
	int32 SaveVersion = 1;

	/** Keyed by USiblingDefinition::SiblingId ("Rafi", "Shira", "Omer"). */
	UPROPERTY(BlueprintReadWrite, Category = "Save")
	TMap<FName, FSiblingSaveData> Siblings;

	UPROPERTY(BlueprintReadWrite, Category = "Save")
	TSet<FName> CompletedMissions;

	UPROPERTY(BlueprintReadWrite, Category = "Save")
	int32 ActiveSiblingIndex = 0;

	/** Rafi's businesses (AMishpachaBusiness ids). */
	UPROPERTY(BlueprintReadWrite, Category = "Save")
	TSet<FName> OwnedBusinesses;

	/** Taken by a rival crew at some point: half price to buy back. */
	UPROPERTY(BlueprintReadWrite, Category = "Save")
	TSet<FName> LostBusinesses;
};
