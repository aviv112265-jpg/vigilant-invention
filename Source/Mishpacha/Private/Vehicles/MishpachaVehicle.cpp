// MISHPACHA - drivable Chaos vehicle.
#include "Vehicles/MishpachaVehicle.h"
#include "Characters/MishpachaCharacter.h"
#include "Heat/CrimeLibrary.h"
#include "ChaosVehicleMovementComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Mishpacha.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AMishpachaVehicle::AMishpachaVehicle()
{
	PrimaryActorTick.bCanEverTick = true;

	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionProfileName(FName(TEXT("Vehicle")));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetMesh());
	CameraBoom->TargetArmLength = 650.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 150.f);
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bInheritYaw = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 6.f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	LookPitch = DefaultCameraPitch;

	TemplateVehicleClass = TSoftClassPtr<AWheeledVehiclePawn>(FSoftObjectPath(TEXT("/Game/VehicleTemplate/Blueprints/SportsCar/BP_VehicleAdvSportsCar.BP_VehicleAdvSportsCar_C")));
}

bool AMishpachaVehicle::HasVehicleSetup() const
{
	if (GetMesh() && GetMesh()->GetSkeletalMeshAsset())
	{
		return true;
	}
	return !TemplateVehicleClass.IsNull() && TemplateVehicleClass.LoadSynchronous() != nullptr;
}

void AMishpachaVehicle::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Runs on every machine (server and clients), so all of them build the same car.
	if (GetMesh() && !GetMesh()->GetSkeletalMeshAsset() && !TemplateVehicleClass.IsNull())
	{
		if (const UClass* TemplateClass = TemplateVehicleClass.LoadSynchronous())
		{
			ApplyTemplate(TemplateClass->GetDefaultObject<AWheeledVehiclePawn>());
		}
		else
		{
			UE_LOG(LogMishpacha, Warning, TEXT("%s has no mesh and the template vehicle '%s' is missing (add the Vehicle content pack)."),
				*GetName(), *TemplateVehicleClass.ToString());
		}
	}
}

void AMishpachaVehicle::ApplyTemplate(const AWheeledVehiclePawn* Template)
{
	USkeletalMeshComponent* SourceMesh = Template ? Template->GetMesh() : nullptr;
	USkeletalMeshComponent* MyMesh = GetMesh();
	if (!SourceMesh || !SourceMesh->GetSkeletalMeshAsset() || !MyMesh)
	{
		return;
	}

	// Handling first (wheels, engine, gearbox, steering, mass ...), so the physics vehicle that
	// the new mesh builds already has them.
	UChaosWheeledVehicleMovementComponent* Source = Cast<UChaosWheeledVehicleMovementComponent>(Template->GetVehicleMovementComponent());
	UChaosWheeledVehicleMovementComponent* Target = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	if (Source && Target)
	{
		for (TFieldIterator<FProperty> It(UChaosWheeledVehicleMovementComponent::StaticClass()); It; ++It)
		{
			const FProperty* Property = *It;
			const UClass* OwnerClass = Property->GetOwnerClass();
			if (!OwnerClass || !OwnerClass->IsChildOf(UChaosVehicleMovementComponent::StaticClass())
				|| !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient))
			{
				continue;
			}
			Property->CopyCompleteValue_InContainer(Target, Source);
		}
	}

	// Rig: skeletal mesh (brings its physics asset), animation blueprint (wheel bones), material overrides.
	MyMesh->SetSkeletalMeshAsset(SourceMesh->GetSkeletalMeshAsset());
	MyMesh->SetAnimInstanceClass(SourceMesh->AnimClass);
	MyMesh->VisibilityBasedAnimTickOption = SourceMesh->VisibilityBasedAnimTickOption;
	MyMesh->SetVisibility(SourceMesh->IsVisible());
	for (int32 Index = 0; Index < SourceMesh->OverrideMaterials.Num(); ++Index)
	{
		if (UMaterialInterface* Material = SourceMesh->OverrideMaterials[Index])
		{
			MyMesh->SetMaterial(Index, Material);
		}
	}

	// Look: vehicle templates often hide the skeletal mesh and show static parts (body, glass,
	// wheels) attached to its bones. Copy those from the Blueprint's construction script.
	for (const UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Template->GetClass()); BPClass;
		BPClass = Cast<UBlueprintGeneratedClass>(BPClass->GetSuperClass()))
	{
		if (!BPClass->SimpleConstructionScript)
		{
			continue;
		}
		for (const USCS_Node* Node : BPClass->SimpleConstructionScript->GetAllNodes())
		{
			const UStaticMeshComponent* Part = Node ? Cast<UStaticMeshComponent>(Node->ComponentTemplate) : nullptr;
			if (!Part || !Part->GetStaticMesh() || !Part->IsVisible())
			{
				continue;
			}
			UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(this, Node->GetVariableName(), RF_Transient, const_cast<UStaticMeshComponent*>(Part));
			Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision); // the physics asset is the car's collision
			Copy->SetupAttachment(MyMesh, Node->AttachToName);
			Copy->RegisterComponent();
			AddInstanceComponent(Copy);
		}
	}

	// Rebuild the physics vehicle with the new wheels.
	if (Target)
	{
		Target->RecreatePhysicsState();
	}
}

void AMishpachaVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaVehicle, Driver);
	DOREPLIFETIME(AMishpachaVehicle, RadioStation);
}

void AMishpachaVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsLocallyControlled() || !CameraBoom)
	{
		return;
	}

	// Free look that drifts back behind the car when the mouse is idle.
	TimeSinceLookInput += DeltaSeconds;
	if (TimeSinceLookInput > LookRecenterDelay)
	{
		LookYaw = FMath::FInterpTo(LookYaw, 0.f, DeltaSeconds, 3.f);
		LookPitch = FMath::FInterpTo(LookPitch, DefaultCameraPitch, DeltaSeconds, 3.f);
	}
	CameraBoom->SetRelativeRotation(FRotator(LookPitch, LookYaw, 0.f));
}

void AMishpachaVehicle::Look(const FVector2D& Axis)
{
	LookYaw = FMath::Clamp(LookYaw + Axis.X * LookSensitivity, -170.f, 170.f);
	LookPitch = FMath::Clamp(LookPitch - Axis.Y * LookSensitivity, -45.f, 15.f);
	TimeSinceLookInput = 0.f;
}

void AMishpachaVehicle::SetThrottle(float Value)
{
	if (UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovement())
	{
		VehicleMovement->SetThrottleInput(Value);
	}
}

void AMishpachaVehicle::SetBrake(float Value)
{
	if (UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovement())
	{
		VehicleMovement->SetBrakeInput(Value);
	}
}

void AMishpachaVehicle::SetSteering(float Value)
{
	if (UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovement())
	{
		VehicleMovement->SetSteeringInput(Value);
	}
}

void AMishpachaVehicle::SetHandbrake(bool bEngaged)
{
	if (UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovement())
	{
		VehicleMovement->SetHandbrakeInput(bEngaged);
	}
}

bool AMishpachaVehicle::EnterVehicle(AMishpachaCharacter* Character)
{
	if (!HasAuthority() || !Character || Driver || Character->IsDead())
	{
		return false;
	}

	AController* CharacterController = Character->GetController();
	if (!CharacterController)
	{
		return false;
	}

	if (AController* Occupant = GetController())
	{
		if (Occupant->IsPlayerController())
		{
			// Another player is driving.
			return false;
		}

		// AI driver -> carjacking (always noticed).
		Occupant->UnPossess();
		Occupant->Destroy();
		UCrimeLibrary::ReportCrimeByActor(Character, ECrimeType::Carjacking, GetActorLocation(), true);
	}
	else if (CharacterController->PlayerState && LastDriverPlayerState.Get() != CharacterController->PlayerState)
	{
		UCrimeLibrary::ReportCrimeByActor(Character, ECrimeType::VehicleTheft, GetActorLocation());
	}

	LastDriverPlayerState = CharacterController->PlayerState;
	Driver = Character;
	Character->OnEnteredVehicle(this);
	CharacterController->Possess(this);
	BP_OnDriverChanged(Character);
	return true;
}

void AMishpachaVehicle::ExitVehicle()
{
	if (!HasAuthority() || !Driver)
	{
		return;
	}

	AMishpachaCharacter* Exiting = Driver;
	AController* DriverController = GetController();
	const FVector ExitLocation = FindExitLocation(Exiting);

	if (UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovement())
	{
		VehicleMovement->SetThrottleInput(0.f);
		VehicleMovement->SetSteeringInput(0.f);
		VehicleMovement->SetBrakeInput(0.f);
		VehicleMovement->SetHandbrakeInput(true);
	}

	Driver = nullptr;
	Exiting->OnExitedVehicle(ExitLocation, FRotator(0.f, GetActorRotation().Yaw, 0.f));
	if (DriverController)
	{
		DriverController->Possess(Exiting);
	}
	BP_OnDriverChanged(nullptr);
}

void AMishpachaVehicle::RequestExit()
{
	if (HasAuthority())
	{
		ExitVehicle();
	}
	else
	{
		ServerRequestExit();
	}
}

void AMishpachaVehicle::ServerRequestExit_Implementation()
{
	ExitVehicle();
}

FVector AMishpachaVehicle::FindExitLocation(const AMishpachaCharacter* Character) const
{
	const FTransform& VehicleTransform = GetActorTransform();

	float CapsuleRadius = 42.f;
	float CapsuleHalfHeight = 96.f;
	if (Character && Character->GetCapsuleComponent())
	{
		Character->GetCapsuleComponent()->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	}

	const FVector Candidates[] =
	{
		DriverExitOffset,
		FVector(DriverExitOffset.X, -DriverExitOffset.Y, DriverExitOffset.Z),
		FVector(-RearExitDistance, 0.f, DriverExitOffset.Z),
		FVector(0.f, 0.f, RoofExitHeight)
	};

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleExit), false, this);
	Params.AddIgnoredActor(Character);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);

	for (const FVector& LocalOffset : Candidates)
	{
		const FVector WorldLocation = VehicleTransform.TransformPosition(LocalOffset);
		if (!GetWorld()->OverlapBlockingTestByChannel(WorldLocation, FQuat::Identity, ECC_Pawn, Capsule, Params))
		{
			return WorldLocation;
		}
	}
	return VehicleTransform.TransformPosition(FVector(0.f, 0.f, RoofExitHeight));
}

void AMishpachaVehicle::NextRadioStation()
{
	if (HasAuthority())
	{
		ServerNextRadioStation_Implementation();
	}
	else
	{
		ServerNextRadioStation();
	}
}

void AMishpachaVehicle::ServerNextRadioStation_Implementation()
{
	RadioStation = (RadioStation + 1) % FMath::Max(1, NumRadioStations);
	OnRep_RadioStation();
}

void AMishpachaVehicle::OnRep_RadioStation()
{
	BP_OnRadioStationChanged(RadioStation);
}
