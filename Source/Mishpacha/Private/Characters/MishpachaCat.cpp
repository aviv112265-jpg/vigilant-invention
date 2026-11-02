// MISHPACHA - Tel Aviv street cat.
#include "Characters/MishpachaCat.h"
#include "AI/MishpachaCivilianAIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"

AMishpachaCat::AMishpachaCat()
{
	PrimaryActorTick.bCanEverTick = true;
	Tags.AddUnique(FName(TEXT("Animal")));
	AIControllerClass = AMishpachaCivilianAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bAutoVisuals = false;
	DefaultWeaponClass = nullptr;
	Loadout.Reset();
	MaxHealth = 20.f;
	Health = MaxHealth;
	JogSpeed = 140.f;
	SprintSpeed = 650.f;
	BodyLifeSpan = 15.f;

	GetCapsuleComponent()->InitCapsuleSize(18.f, 16.f);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -16.f));
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->MaxWalkSpeed = JogSpeed;
	MoveComp->RotationRate = FRotator(0.f, 720.f, 0.f);
}

void AMishpachaCat::BeginPlay()
{
	Super::BeginPlay();
	BuildBody();
}

void AMishpachaCat::BuildBody()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Cube || !Sphere || !Cone || !Basic)
	{
		return;
	}

	// Ginger tabby, black, gray, cream or white-and-black - the same coat on every machine.
	const FVector Spot = GetActorLocation();
	FRandomStream Stream(FMath::FloorToInt32(Spot.X / 50.f) * 13 + FMath::FloorToInt32(Spot.Y / 50.f) * 7);
	const FLinearColor Coats[] = { FLinearColor(0.55f, 0.22f, 0.05f), FLinearColor(0.015f, 0.015f, 0.015f), FLinearColor(0.25f, 0.25f, 0.26f),
		FLinearColor(0.7f, 0.6f, 0.45f), FLinearColor(0.85f, 0.85f, 0.83f) };
	const FLinearColor Coat = Coats[Stream.RandRange(0, UE_ARRAY_COUNT(Coats) - 1)];
	UMaterialInstanceDynamic* Fur = UMaterialInstanceDynamic::Create(Basic, this);
	Fur->SetVectorParameterValue(TEXT("Color"), Coat);

	auto Add = [&](USceneComponent* Parent, UStaticMesh* Shape, const FVector& Location, const FVector& Size, const FRotator& Rotation)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
		Part->SetStaticMesh(Shape);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(true);
		Part->SetupAttachment(Parent);
		Part->SetUsingAbsoluteScale(true);
		Part->SetRelativeLocationAndRotation(Location, Rotation);
		Part->SetRelativeScale3D(Size / 100.f);
		Part->SetMaterial(0, Fur);
		Part->RegisterComponent();
		return Part;
	};

	// Body center 22 cm off the ground (capsule bottom is at -16 from the actor).
	CatBody = Add(GetCapsuleComponent(), Sphere, FVector(0.f, 0.f, 6.f), FVector(42.f, 18.f, 17.f), FRotator::ZeroRotator);
	Add(CatBody, Sphere, FVector(22.f, 0.f, 9.f), FVector(15.f, 14.f, 13.f), FRotator::ZeroRotator);             // head
	Add(CatBody, Cone, FVector(23.f, 4.f, 17.f), FVector(5.f, 4.f, 6.f), FRotator(0.f, 0.f, 12.f));             // ears
	Add(CatBody, Cone, FVector(23.f, -4.f, 17.f), FVector(5.f, 4.f, 6.f), FRotator(0.f, 0.f, -12.f));
	Tail = Add(CatBody, Cube, FVector(-24.f, 0.f, 8.f), FVector(4.f, 4.f, 26.f), FRotator(-35.f, 0.f, 0.f));
	const FVector LegSpots[] = { FVector(13.f, 6.f, -12.f), FVector(13.f, -6.f, -12.f), FVector(-13.f, 6.f, -12.f), FVector(-13.f, -6.f, -12.f) };
	for (const FVector& LegSpot : LegSpots)
	{
		Legs.Add(Add(CatBody, Cube, LegSpot, FVector(4.f, 4.f, 14.f), FRotator::ZeroRotator));
	}
}

void AMishpachaCat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!CatBody || IsDead() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Trot: diagonal legs swing together, faster with speed; the tail sways.
	const float Speed = GetVelocity().Size2D();
	TrotPhase += DeltaSeconds * (2.f + Speed / 25.f);
	const float Swing = FMath::Min(1.f, Speed / 120.f) * 28.f;
	for (int32 Leg = 0; Leg < Legs.Num(); ++Leg)
	{
		const float Sign = (Leg == 0 || Leg == 3) ? 1.f : -1.f;
		Legs[Leg]->SetRelativeRotation(FRotator(FMath::Sin(TrotPhase) * Swing * Sign, 0.f, 0.f));
	}
	if (Tail)
	{
		Tail->SetRelativeRotation(FRotator(-35.f, 0.f, FMath::Sin(TrotPhase * 0.4f) * 18.f));
	}
	CatBody->SetRelativeLocation(FVector(0.f, 0.f, 6.f + FMath::Abs(FMath::Sin(TrotPhase)) * Swing * 0.05f));
}

void AMishpachaCat::OnDeathVisuals()
{
	if (!CatBody)
	{
		return;
	}
	CatBody->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	CatBody->SetCollisionResponseToAllChannels(ECR_Block);
	CatBody->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	CatBody->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	CatBody->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CatBody->SetSimulatePhysics(true);
	CatBody->AddImpulse(DeathImpulse.IsNearlyZero() ? FVector(0.f, 0.f, 150.f) : FVector(DeathImpulse) * 0.8f, NAME_None, true);
}
