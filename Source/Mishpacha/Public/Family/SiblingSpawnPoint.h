// MISHPACHA - marks a sibling's "home" start location in the level.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiblingSpawnPoint.generated.h"

class UArrowComponent;

/** Place one per sibling in the level and set SiblingId ("Rafi", "Shira", "Omer"). */
UCLASS()
class MISHPACHA_API ASiblingSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ASiblingSpawnPoint();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Family")
	FName SiblingId;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Family")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Family")
	TObjectPtr<UArrowComponent> Arrow;
};
