// MISHPACHA - patrol car.
#include "Vehicles/MishpachaPoliceCar.h"
#include "AI/MishpachaPoliceDriverAI.h"
#include "Characters/MishpachaNPCCharacters.h"
#include "Heat/PoliceDispatchSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMishpachaPoliceCar::AMishpachaPoliceCar()
{
	Tags.AddUnique(FName(TEXT("Police")));
	AIControllerClass = AMishpachaPoliceDriverAI::StaticClass();
	AutoPossessAI = EAutoPossessAI::Spawned;
	OfficerClass = AMishpachaPolicePawn::StaticClass();
	PaintColor = FLinearColor(0.9f, 0.9f, 0.92f, 1.f);

	// Light bar: two halves on the roof, red and blue (M_Siren flashes them out of phase).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto MakeHalf = [this](const TCHAR* Name, float Side) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Half = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Half->SetupAttachment(GetMesh());
		Half->SetRelativeLocation(FVector(-25.f, Side * 26.f, 126.f));
		Half->SetRelativeScale3D(FVector(0.22f, 0.5f, 0.1f));
		Half->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Half->SetCastShadow(false);
		return Half;
	};
	LightBarRed = MakeHalf(TEXT("LightBarRed"), -1.f);
	LightBarBlue = MakeHalf(TEXT("LightBarBlue"), 1.f);
	if (CubeMesh.Succeeded())
	{
		LightBarRed->SetStaticMesh(CubeMesh.Object);
		LightBarBlue->SetStaticMesh(CubeMesh.Object);
	}
}

void AMishpachaPoliceCar::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* Siren = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mishpacha/Materials/M_Siren.M_Siren"), nullptr, LOAD_Quiet | LOAD_NoWarn))
	{
		auto Paint = [Siren](UStaticMeshComponent* Half, const FLinearColor& Color, float Phase)
		{
			if (UMaterialInstanceDynamic* MID = Half->CreateDynamicMaterialInstance(0, Siren))
			{
				MID->SetVectorParameterValue(FName(TEXT("Color")), Color);
				MID->SetScalarParameterValue(FName(TEXT("Phase")), Phase);
			}
		};
		Paint(LightBarRed, FLinearColor(1.f, 0.03f, 0.02f), 0.f);
		Paint(LightBarBlue, FLinearColor(0.05f, 0.2f, 1.f), 0.5f);
	}
}

void AMishpachaPoliceCar::DeployOfficers(APawn* Target)
{
	if (!HasAuthority() || bDeployed || !OfficerClass)
	{
		return;
	}
	bDeployed = true;

	UWorld* World = GetWorld();
	UPoliceDispatchSubsystem* Dispatch = World->GetSubsystem<UPoliceDispatchSubsystem>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	for (int32 Index = 0; Index < OfficerCount; ++Index)
	{
		// Out of the doors: left, right, then behind.
		const FVector Offset = Index == 0 ? FVector(10.f, -240.f, 60.f) : (Index == 1 ? FVector(10.f, 240.f, 60.f) : FVector(-380.f, (Index - 2.5f) * 120.f, 60.f));
		const FVector Location = GetActorTransform().TransformPosition(Offset);
		const FRotator Facing = Target ? FRotator(0.f, (Target->GetActorLocation() - Location).Rotation().Yaw, 0.f) : GetActorRotation();
		if (APawn* Officer = World->SpawnActor<APawn>(OfficerClass, Location, Facing, Params))
		{
			if (!Officer->GetController())
			{
				Officer->SpawnDefaultController();
			}
			if (Dispatch)
			{
				Dispatch->RegisterUnit(Officer, Target);
			}
		}
	}
}
