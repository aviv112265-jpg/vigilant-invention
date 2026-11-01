// MISHPACHA - Omer's remote-controlled drone.
#include "Abilities/DronePawn.h"
#include "Abilities/OmerDroneAbility.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

ADronePawn::ADronePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	AutoPossessAI = EAutoPossessAI::Disabled;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(30.f);
	Collision->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	RootComponent = Collision;

	DroneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DroneMesh"));
	DroneMesh->SetupAttachment(Collision);
	DroneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collision);
	Camera->bUsePawnControlRotation = true;

	DroneMovement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("DroneMovement"));
	DroneMovement->UpdatedComponent = Collision;
	DroneMovement->MaxSpeed = 1800.f;
	DroneMovement->Acceleration = 3000.f;
	DroneMovement->Deceleration = 4000.f;

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
}

void ADronePawn::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimer(MarkTimer, this, &ADronePawn::MarkEnemies, FMath::Max(MarkInterval, 0.1f), true);
}

void ADronePawn::InitDrone(UOmerDroneAbility* InAbility, AActor* InOperator)
{
	Ability = InAbility;
	OperatorActor = InOperator;
}

void ADronePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		if (!FMath::IsNearlyZero(VerticalInput))
		{
			AddMovementInput(FVector::UpVector, VerticalInput);
		}

		// Remote pilots push their position to the server (simulated proxies get it via movement replication).
		if (!HasAuthority())
		{
			TimeSinceTransformSync += DeltaSeconds;
			if (TimeSinceTransformSync >= 0.05f)
			{
				TimeSinceTransformSync = 0.f;
				ServerSyncTransform(GetActorLocation(), GetActorRotation());
			}
		}
	}

	if (HasAuthority())
	{
		const AActor* Operator = OperatorActor.Get();
		if (!Operator || FVector::Dist(Operator->GetActorLocation(), GetActorLocation()) > MaxSignalRange)
		{
			RecallNow();
		}
	}
}

void ADronePawn::MoveInput(const FVector2D& Axis)
{
	if (!Controller)
	{
		return;
	}
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void ADronePawn::LookInput(const FVector2D& Axis)
{
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ADronePawn::RequestRecall()
{
	if (HasAuthority())
	{
		RecallNow();
	}
	else
	{
		ServerRequestRecall();
	}
}

void ADronePawn::ServerRequestRecall_Implementation()
{
	RecallNow();
}

void ADronePawn::ServerSyncTransform_Implementation(FVector_NetQuantize NewLocation, FRotator NewRotation)
{
	SetActorLocationAndRotation(NewLocation, NewRotation);
}

void ADronePawn::RecallNow()
{
	if (!HasAuthority() || IsActorBeingDestroyed())
	{
		return;
	}

	if (UOmerDroneAbility* OwnerAbility = Ability.Get())
	{
		// Ends the ability: repossesses Omer and destroys this drone.
		OwnerAbility->ForceEnd();
	}
	else
	{
		Destroy();
	}
}

void ADronePawn::MarkEnemies()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
	TArray<AActor*> IgnoredActors;
	IgnoredActors.Add(this);
	TArray<AActor*> Found;
	UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), MarkRange, ObjectTypes, APawn::StaticClass(), IgnoredActors, Found);

	for (AActor* Candidate : Found)
	{
		if (!Candidate || !Candidate->ActorHasTag(EnemyTag) || MarkedActors.Contains(Candidate))
		{
			continue;
		}

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DroneMark), false, this);
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Camera->GetComponentLocation(), Candidate->GetActorLocation(), ECC_Visibility, Params);
		if (bHit && Hit.GetActor() != Candidate)
		{
			continue;
		}

		TInlineComponentArray<UPrimitiveComponent*> Primitives(Candidate);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			Primitive->SetRenderCustomDepth(true);
			Primitive->SetCustomDepthStencilValue(MarkStencilValue);
		}
		MarkedActors.Add(Candidate);
		BP_OnEnemyMarked(Candidate);
	}
}
