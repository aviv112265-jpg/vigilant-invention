// MISHPACHA - Tel Aviv street cat: wanders the sidewalks, bolts from gunfire, and can be run over.
#pragma once

#include "CoreMinimal.h"
#include "Characters/MishpachaCharacter.h"
#include "MishpachaCat.generated.h"

/** Tagged "Animal": hurting one is no crime. Built from basic shapes, legs trot in code. */
UCLASS()
class MISHPACHA_API AMishpachaCat : public AMishpachaCharacter
{
	GENERATED_BODY()

public:
	AMishpachaCat();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void OnDeathVisuals() override;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CatBody;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Legs;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Tail;

	float TrotPhase = 0.f;

	void BuildBody();
};
