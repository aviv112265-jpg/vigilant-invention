// MISHPACHA - GPS route painted on the road (local player only).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MishpachaRouteGuide.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Chevrons ( > > > ) laid flat on the asphalt along a route, plus an optional beacon at the end.
 * Spawned and driven by AMishpachaHUD on the owning client; never replicated.
 */
UCLASS(NotPlaceable, Transient)
class MISHPACHA_API AMishpachaRouteGuide : public AActor
{
	GENERATED_BODY()

public:
	AMishpachaRouteGuide();

	/** Points in world space, on the road surface. */
	void ShowRoute(const TArray<FVector>& Points, const FLinearColor& Color, bool bShowBeacon);
	void HideRoute();

	/** Distance between chevrons (cm). */
	float Spacing = 450.f;

	/** Cap on chevrons drawn (from the start of the route). */
	int32 MaxChevrons = 260;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Route")
	TObjectPtr<UInstancedStaticMeshComponent> Chevrons;

	UPROPERTY(VisibleAnywhere, Category = "Route")
	TObjectPtr<UStaticMeshComponent> Beacon;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
};
