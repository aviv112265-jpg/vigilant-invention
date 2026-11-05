// MISHPACHA - drivable Chaos vehicle.
#include "Vehicles/MishpachaVehicle.h"
#include "Characters/MishpachaCharacter.h"
#include "Heat/CrimeLibrary.h"
#include "ChaosVehicleMovementComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "World/MishpachaSounds.h"
#include "World/MishpachaDayNight.h"
#include "World/MishpachaStreetProp.h"
#include "Components/SpotLightComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Core/MishpachaPlayerController.h"
#include "Mishpacha.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialTypes.h"
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
	OnRep_PaintColor(); // a class default paint (police white) applies to the copied body
}

void AMishpachaVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaVehicle, Driver);
	DOREPLIFETIME(AMishpachaVehicle, RadioStation);
	DOREPLIFETIME(AMishpachaVehicle, PaintColor);
	DOREPLIFETIME(AMishpachaVehicle, bLightsSwitchedOff);
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

void AMishpachaVehicle::ToggleLights()
{
	if (HasAuthority())
	{
		ServerToggleLights_Implementation();
	}
	else
	{
		ServerToggleLights();
	}
}

void AMishpachaVehicle::ServerToggleLights_Implementation()
{
	bLightsSwitchedOff = !bLightsSwitchedOff;
	OnRep_LightsSwitchedOff();
}

void AMishpachaVehicle::OnRep_LightsSwitchedOff()
{
	if (AMishpachaPlayerController* PC = IsLocallyControlled() ? Cast<AMishpachaPlayerController>(GetController()) : nullptr)
	{
		PC->ClientShowNotification(bLightsSwitchedOff
			? NSLOCTEXT("MishpachaVehicle", "LightsOff", "Lights OFF - harder for the police to spot you at night")
			: NSLOCTEXT("MishpachaVehicle", "LightsAuto", "Lights: automatic"));
	}
	UpdateLights();
}

float AMishpachaVehicle::GetPoliceSightScale(const AActor* Target)
{
	const AMishpachaDayNight* Time = Target ? AMishpachaDayNight::Find(Target) : nullptr;
	const float Night = Time ? Time->GetNightAmount() : 0.f;
	if (Night < 0.3f)
	{
		return 1.f;
	}
	const AMishpachaVehicle* Car = Cast<AMishpachaVehicle>(Target);
	if (const AMishpachaCharacter* Character = Cast<AMishpachaCharacter>(Target))
	{
		Car = Character->GetCurrentVehicle();
	}
	if (Car)
	{
		return Car->AreLightsOn() ? 1.f : FMath::Lerp(1.f, 0.35f, Night);
	}
	return FMath::Lerp(1.f, 0.7f, Night);
}

void AMishpachaVehicle::BuildLights()
{
	USkeletalMeshComponent* Body = GetMesh();
	const USkeletalMesh* BodyMesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mishpacha/Materials/M_CarLight.M_CarLight"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!BodyMesh || !Cube || !Glow)
	{
		UE_LOG(LogMishpacha, Warning, TEXT("%s: no headlights (mesh %d, cube %d, M_CarLight %d)."), *GetName(), BodyMesh != nullptr, Cube != nullptr, Glow != nullptr);
		return;
	}
	bLightsBuilt = true;

	// Corners of the car (vehicle space: +X forward, +Z up): the visible body parts if there are any
	// (vehicle templates show static parts on a hidden skeleton), else the skeletal mesh.
	FBox Box(ForceInit);
	TArray<UStaticMeshComponent*> BodyParts;
	GetComponents<UStaticMeshComponent>(BodyParts);
	for (const UStaticMeshComponent* Part : BodyParts)
	{
		if (Part->IsVisible() && Part->GetStaticMesh())
		{
			Box += Part->CalcBounds(Part->GetComponentTransform().GetRelativeTransform(GetActorTransform())).GetBox();
		}
	}
	if (!Box.IsValid)
	{
		Box = BodyMesh->GetBounds().GetBox();
	}
	const float FrontX = Box.Max.X - 4.f;
	const float RearX = Box.Min.X + 4.f;
	const float SideY = Box.GetExtent().Y * 0.62f;
	const float LightZ = Box.Min.Z + Box.GetSize().Z * 0.42f;

	HeadGlow = UMaterialInstanceDynamic::Create(Glow, this);
	HeadGlow->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.92f, 0.78f));
	TailGlow = UMaterialInstanceDynamic::Create(Glow, this);
	TailGlow->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.03f, 0.02f));

	auto AddLamp = [&](const FVector& Location, const FVector& Size, UMaterialInstanceDynamic* Material)
	{
		UStaticMeshComponent* Lamp = NewObject<UStaticMeshComponent>(this);
		Lamp->SetStaticMesh(Cube);
		Lamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lamp->SetCastShadow(false);
		Lamp->SetupAttachment(Body);
		Lamp->SetRelativeLocation(Location);
		Lamp->SetRelativeScale3D(Size / 100.f);
		Lamp->SetMaterial(0, Material);
		Lamp->RegisterComponent();
	};
	for (const float Side : { -1.f, 1.f })
	{
		AddLamp(FVector(FrontX, Side * SideY, LightZ), FVector(6.f, 32.f, 9.f), HeadGlow);
		AddLamp(FVector(RearX, Side * SideY, LightZ + 8.f), FVector(6.f, 36.f, 8.f), TailGlow);
	}

	HeadBeam = NewObject<USpotLightComponent>(this);
	HeadBeam->SetupAttachment(Body);
	HeadBeam->SetRelativeLocationAndRotation(FVector(FrontX + 10.f, 0.f, LightZ), FRotator(-6.f, 0.f, 0.f));
	HeadBeam->SetIntensityUnits(ELightUnits::Candelas);
	HeadBeam->SetIntensity(1200.f);
	HeadBeam->SetLightColor(FLinearColor(1.f, 0.93f, 0.8f));
	HeadBeam->SetAttenuationRadius(3500.f);
	HeadBeam->SetInnerConeAngle(16.f);
	HeadBeam->SetOuterConeAngle(34.f);
	HeadBeam->SetCastShadows(false);
	HeadBeam->SetVisibility(false);
	HeadBeam->RegisterComponent();
}

void AMishpachaVehicle::UpdateLights()
{
	if (!bLightsBuilt)
	{
		return;
	}
	if (!DayNight.IsValid())
	{
		DayNight = AMishpachaDayNight::Find(this);
	}
	const float Night = DayNight.IsValid() ? DayNight->GetNightAmount() : 0.f;

	// On by themselves after dark whenever someone (player or AI) is driving, unless switched off.
	const bool bOccupied = HasDriver() || GetController() != nullptr;
	const bool bWasOn = bLightsOn;
	bLightsOn = bOccupied && Night > 0.35f && !bLightsSwitchedOff;
	if (bLightsOn != bWasOn && IsLocallyControlled())
	{
		UE_LOG(LogMishpacha, Log, TEXT("%s: headlights %s (night %.2f)."), *GetName(), bLightsOn ? TEXT("on") : TEXT("off"), Night);
	}

	// Brake lights: braking, or reversing.
	UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovementComponent();
	const bool bBraking = bOccupied && VehicleMovement && VehicleMovement->GetBrakeInput() > 0.1f;
	HeadGlow->SetScalarParameterValue(TEXT("Glow"), bLightsOn ? 40.f : 0.05f);
	TailGlow->SetScalarParameterValue(TEXT("Glow"), bBraking ? 30.f : (bLightsOn ? 8.f : 0.05f));
	if (HeadBeam)
	{
		HeadBeam->SetVisibility(bLightsOn);
	}
}

// ---------------------------------------------------------------------------
// Running things over
// ---------------------------------------------------------------------------

void AMishpachaVehicle::CheckBumper(float DeltaSeconds)
{
	const FVector Velocity = GetVelocity();
	const float Speed = Velocity.Size();
	const USkeletalMesh* BodyMesh = GetMesh() ? GetMesh()->GetSkeletalMeshAsset() : nullptr;
	if (Speed < 250.f || !BodyMesh)
	{
		return;
	}

	// A box just past the bumper the car is moving toward, as long as one or two frames of travel.
	const FBox Box = BodyMesh->GetBounds().GetBox();
	const FTransform& Transform = GetActorTransform();
	const bool bForward = (Velocity | GetActorForwardVector()) >= 0.f;
	const float Reach = Speed * DeltaSeconds * 2.f + 50.f;
	const float BumperX = bForward ? Box.Max.X : Box.Min.X;
	const FVector LocalCenter(BumperX + (bForward ? 1.f : -1.f) * (Reach * 0.5f - 20.f), 0.f, Box.Min.Z + Box.GetSize().Z * 0.5f);
	const FVector HalfSize(Reach * 0.5f + 20.f, Box.GetExtent().Y * 0.95f, Box.GetExtent().Z);

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleBumper), false, this);
	if (Driver)
	{
		Params.AddIgnoredActor(Driver);
	}
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Transform.TransformPosition(LocalCenter), Transform.GetRotation(), Objects,
		FCollisionShape::MakeBox(HalfSize), Params);

	const double Now = GetWorld()->GetTimeSeconds();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (!Other || Other == this)
		{
			continue;
		}
		const double* Last = RecentBumps.Find(Other);
		if (Last && Now - *Last < 1.0)
		{
			continue;
		}
		if (AMishpachaCharacter* Person = Cast<AMishpachaCharacter>(Other))
		{
			if (!Person->IsDead() && !Person->GetCurrentVehicle())
			{
				RecentBumps.Add(Other, Now);
				Person->HitByVehicle(this, Velocity, GetController());
			}
		}
		else if (AMishpachaStreetProp* Prop = Cast<AMishpachaStreetProp>(Other))
		{
			if (Prop->IsStanding())
			{
				RecentBumps.Add(Other, Now);
				Prop->HitByVehicle(Velocity);
			}
		}
	}
	if (RecentBumps.Num() > 16)
	{
		for (auto It = RecentBumps.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || Now - It.Value() > 2.0)
			{
				It.RemoveCurrent();
			}
		}
	}
}

void AMishpachaVehicle::SetPaintColor(const FLinearColor& NewColor)
{
	if (HasAuthority())
	{
		PaintColor = NewColor;
		OnRep_PaintColor();
	}
}

void AMishpachaVehicle::OnRep_PaintColor()
{
	if (PaintColor.A <= 0.f)
	{
		return;
	}
	static const FName PaintParam(TEXT("Paint Tint"));
	TArray<UPrimitiveComponent*> Parts;
	GetComponents<UPrimitiveComponent>(Parts);
	for (UPrimitiveComponent* Part : Parts)
	{
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Part->GetMaterial(Slot);
			FLinearColor Existing;
			if (!Material || !Material->GetVectorParameterValue(FHashedMaterialParameterInfo(PaintParam), Existing))
			{
				continue;
			}
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material);
			if (!MID)
			{
				MID = Part->CreateDynamicMaterialInstance(Slot, Material);
			}
			if (MID)
			{
				MID->SetVectorParameterValue(PaintParam, PaintColor);
			}
		}
	}
}

void AMishpachaVehicle::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		EngineAudio = MishpachaSounds::MakeLoop(GetMesh(), TEXT("S_Engine_Loop"), 5500.f);
		BuildLights();
	}
}

void AMishpachaVehicle::UpdateSounds()
{
	const float Speed = GetVelocity().Size();
	const float SpeedFactor = FMath::Clamp(Speed / 2500.f, 0.f, 1.f);

	// Engine: idles while someone sits in it, revs with speed; parked and empty = silent.
	if (EngineAudio)
	{
		const bool bRunning = HasDriver() || GetController() != nullptr || Speed > 150.f;
		if (bRunning && !EngineAudio->IsPlaying())
		{
			EngineAudio->Play();
		}
		else if (!bRunning && EngineAudio->IsPlaying())
		{
			EngineAudio->Stop();
		}
		if (bRunning)
		{
			UChaosVehicleMovementComponent* VehicleMovement = GetVehicleMovementComponent();
			const float Throttle = VehicleMovement ? VehicleMovement->GetThrottleInput() : 0.f;
			EngineAudio->SetPitchMultiplier(0.55f + SpeedFactor * 1.3f + Throttle * 0.15f);
			EngineAudio->SetVolumeMultiplier(0.35f + SpeedFactor * 0.45f + Throttle * 0.2f);
		}
	}

	// Radio: only for the local driver, station 0 = off.
	const int32 WantedStation = (IsLocallyControlled() && HasDriver()) ? RadioStation : 0;
	if (WantedStation != PlayingRadioStation)
	{
		if (AMishpachaPlayerController* PC = IsLocallyControlled() ? Cast<AMishpachaPlayerController>(GetController()) : nullptr)
		{
			const FText Names[] = { NSLOCTEXT("MishpachaRadio", "Off", "Radio off"), NSLOCTEXT("MishpachaRadio", "TelAvirFM", "Radio: Tel Avir FM"), NSLOCTEXT("MishpachaRadio", "Mizrahi", "Radio: Mizrahi Wave"), NSLOCTEXT("MishpachaRadio", "YamFM", "Radio: Yam FM"), NSLOCTEXT("MishpachaRadio", "RockHaIr", "Radio: Rock HaIr") };
			if (RadioStation >= 0 && RadioStation < UE_ARRAY_COUNT(Names) && (WantedStation > 0 || PlayingRadioStation > 0) && HasDriver())
			{
				PC->ClientShowNotification(Names[RadioStation]);
			}
		}
		PlayingRadioStation = WantedStation;
		if (RadioAudio)
		{
			RadioAudio->Stop();
			RadioAudio->DestroyComponent();
			RadioAudio = nullptr;
		}
		static const FName Stations[] = { NAME_None, TEXT("S_Radio_TelAvirFM"), TEXT("S_Radio_MizrahiWave"), TEXT("S_Radio_YamFM"), TEXT("S_Radio_RockHaIr") };
		if (WantedStation > 0 && WantedStation < UE_ARRAY_COUNT(Stations))
		{
			RadioAudio = MishpachaSounds::MakeLoop(GetMesh(), Stations[WantedStation], 0.f, false);
			if (RadioAudio)
			{
				RadioAudio->SetVolumeMultiplier(0.55f);
				RadioAudio->Play();
			}
		}
	}
}

void AMishpachaVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateSounds();
		UpdateLights();
	}
	if (HasAuthority())
	{
		CheckBumper(DeltaSeconds);
	}

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
