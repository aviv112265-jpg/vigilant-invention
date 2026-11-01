// MISHPACHA - GPS route painted on the road (local player only).
#include "World/MishpachaRouteGuide.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AMishpachaRouteGuide::AMishpachaRouteGuide()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Chevrons = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Chevrons"));
	Chevrons->SetupAttachment(Root);
	Chevrons->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Chevrons->SetCastShadow(false);
	if (CubeMesh.Succeeded())
	{
		Chevrons->SetStaticMesh(CubeMesh.Object);
	}

	Beacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Beacon"));
	Beacon->SetupAttachment(Root);
	Beacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beacon->SetCastShadow(false);
	Beacon->SetHiddenInGame(true);
	Beacon->SetAbsolute(true, true, true);
	if (CylinderMesh.Succeeded())
	{
		Beacon->SetStaticMesh(CylinderMesh.Object);
	}
}

void AMishpachaRouteGuide::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn))
	{
		Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Chevrons->SetMaterial(0, Material);
		Beacon->SetMaterial(0, Material);
	}
}

void AMishpachaRouteGuide::HideRoute()
{
	Chevrons->ClearInstances();
	Beacon->SetHiddenInGame(true);
}

void AMishpachaRouteGuide::ShowRoute(const TArray<FVector>& Points, const FLinearColor& Color, bool bShowBeacon)
{
	Chevrons->ClearInstances();
	if (Material)
	{
		Material->SetVectorParameterValue(FName(TEXT("Color")), Color);
	}

	if (Points.Num() < 2)
	{
		Beacon->SetHiddenInGame(true);
		return;
	}

	// Chevron = two flat strokes meeting at a tip that points along the route.
	const float StrokeLength = 170.f;
	const float StrokeWidth = 34.f;
	const float StrokeHeight = 3.f;
	const float StrokeAngle = 38.f;
	const float EndGap = 350.f; // leave the destination itself clear

	double RouteLength = 0.0;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		RouteLength += FVector::Dist2D(Points[Index - 1], Points[Index]);
	}

	TArray<FTransform> Transforms;
	double Travelled = 0.0;
	double NextMark = 250.0;
	for (int32 Index = 1; Index < Points.Num() && Transforms.Num() < MaxChevrons * 2; ++Index)
	{
		const FVector A = Points[Index - 1];
		const FVector B = Points[Index];
		const double SegmentLength = FVector::Dist2D(A, B);
		if (SegmentLength < KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector Dir = FVector(B.X - A.X, B.Y - A.Y, 0.f) / SegmentLength;

		while (NextMark <= Travelled + SegmentLength && NextMark <= RouteLength - EndGap && Transforms.Num() < MaxChevrons * 2)
		{
			const FVector Center = A + Dir * (NextMark - Travelled);
			const FVector Tip = Center + Dir * 45.f;
			for (const float Sign : { 1.f, -1.f })
			{
				const FVector Back = (-Dir).RotateAngleAxis(Sign * StrokeAngle, FVector::UpVector);
				const FVector StrokeCenter = Tip + Back * (StrokeLength * 0.5f) + FVector(0.f, 0.f, StrokeHeight * 0.5f);
				Transforms.Add(FTransform(FRotator(0.f, Back.Rotation().Yaw, 0.f), StrokeCenter,
					FVector(StrokeLength, StrokeWidth, StrokeHeight) / 100.f));
			}
			NextMark += Spacing;
		}
		Travelled += SegmentLength;
	}
	Chevrons->AddInstances(Transforms, false, true);

	Beacon->SetHiddenInGame(!bShowBeacon);
	if (bShowBeacon)
	{
		// Thin pillar (basic cylinder: 100 cm wide, 100 cm tall, pivot at center).
		Beacon->SetWorldLocationAndRotation(Points.Last() + FVector(0.f, 0.f, 1500.f), FRotator::ZeroRotator);
		Beacon->SetWorldScale3D(FVector(0.45f, 0.45f, 30.f));
	}
}
