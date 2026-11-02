// MISHPACHA - street furniture a car can knock down.
#include "World/MishpachaStreetProp.h"
#include "Mishpacha.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	UStaticMesh* LoadShape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	UMaterialInterface* LoadMaterial(const TCHAR* Path)
	{
		return LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	const FLinearColor LampColors[3] = { FLinearColor(1.f, 0.04f, 0.02f), FLinearColor(1.f, 0.55f, 0.02f), FLinearColor(0.05f, 1.f, 0.3f) };
}

AMishpachaStreetProp::AMishpachaStreetProp()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	NetDormancy = DORM_Initial;
	SetNetCullDistanceSquared(FMath::Square(15000.f));

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Pole = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pole"));
	Pole->SetupAttachment(Pivot);
	Pole->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Pole->SetCanEverAffectNavigation(false);
}

void AMishpachaStreetProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaStreetProp, Kind);
	DOREPLIFETIME(AMishpachaStreetProp, Phase);
	DOREPLIFETIME(AMishpachaStreetProp, State);
	DOREPLIFETIME(AMishpachaStreetProp, HitVelocity);
}

void AMishpachaStreetProp::BeginPlay()
{
	Super::BeginPlay();
	Build();
	OnRep_State();
}

void AMishpachaStreetProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LampTimer);
	Super::EndPlay(EndPlayReason);
}

UStaticMeshComponent* AMishpachaStreetProp::AddPart(UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FRotator& Rotation, UMaterialInterface* Material, const FLinearColor& Color)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(Mesh);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCanEverAffectNavigation(false);
	Part->SetupAttachment(Pole);
	Part->RegisterComponent();
	// Parts hang off the (scaled) pole: their offsets undo its scale, their own size is absolute.
	const FVector PoleScale = Pole->GetRelativeScale3D();
	Part->SetUsingAbsoluteScale(true);
	Part->SetRelativeLocationAndRotation(Location / PoleScale, Rotation);
	Part->SetRelativeScale3D(Size / 100.f);
	if (Material)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		Part->SetMaterial(0, MID);
	}
	Parts.Add(Part);
	return Part;
}

void AMishpachaStreetProp::Build()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;

	UStaticMesh* Cube = LoadShape(TEXT("Cube"));
	UStaticMesh* Cylinder = LoadShape(TEXT("Cylinder"));
	UMaterialInterface* Basic = LoadMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	UMaterialInterface* Glow = LoadMaterial(TEXT("/Game/Mishpacha/Materials/M_CarLight.M_CarLight"));
	if (!Cube || !Cylinder)
	{
		return;
	}

	// Pole: basic cylinder is 100 cm tall with its pivot in the middle.
	const float Height = Kind == EMishpachaPropKind::TrafficLight ? 420.f : 290.f;
	Pole->SetStaticMesh(Cylinder);
	Pole->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
	Pole->SetRelativeScale3D(FVector(0.1f, 0.1f, Height / 100.f));
	if (Basic)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Basic, this);
		MID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.22f, 0.23f, 0.24f));
		Pole->SetMaterial(0, MID);
	}
	const float Top = Height * 0.5f; // pole-space top, before un-scaling (AddPart divides by the scale)

	switch (Kind)
	{
	case EMishpachaPropKind::TrafficLight:
	{
		// Black box facing +X with red / yellow / green lamps, and a yellow-black backplate.
		AddPart(Cube, FVector(4.f, 0.f, Top - 60.f), FVector(28.f, 38.f, 105.f), FRotator::ZeroRotator, Basic, FLinearColor(0.85f, 0.65f, 0.05f));
		AddPart(Cube, FVector(14.f, 0.f, Top - 60.f), FVector(22.f, 30.f, 95.f), FRotator::ZeroRotator, Basic, FLinearColor(0.02f, 0.02f, 0.02f));
		for (int32 Lamp = 0; Lamp < 3; ++Lamp)
		{
			UStaticMeshComponent* Bulb = AddPart(Cylinder, FVector(26.f, 0.f, Top - 28.f - Lamp * 30.f), FVector(22.f, 22.f, 4.f), FRotator(90.f, 0.f, 0.f), Glow ? Glow : Basic, LampColors[Lamp]);
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Bulb->GetMaterial(0)))
			{
				Lamps.Add(MID);
			}
		}
		UpdateLamps();
		GetWorldTimerManager().SetTimer(LampTimer, this, &AMishpachaStreetProp::UpdateLamps, 0.5f, true, FMath::FRand() * 0.5f);
		break;
	}
	case EMishpachaPropKind::StopSign:
		// Red disc with a white band ("STOP" without the letters).
		AddPart(Cylinder, FVector(6.f, 0.f, Top - 25.f), FVector(62.f, 62.f, 3.f), FRotator(90.f, 0.f, 0.f), Basic, FLinearColor(0.7f, 0.02f, 0.02f));
		AddPart(Cube, FVector(8.f, 0.f, Top - 25.f), FVector(1.f, 44.f, 12.f), FRotator::ZeroRotator, Basic, FLinearColor(0.95f, 0.95f, 0.95f));
		break;
	case EMishpachaPropKind::StreetSign:
		// Tel Aviv street-name plate: white with a blue frame, two of them crossed.
		for (int32 Arm = 0; Arm < 2; ++Arm)
		{
			const FRotator Yaw(0.f, Arm * 90.f, 0.f);
			const FVector Out = Yaw.RotateVector(FVector(0.f, 45.f, Top - 15.f - Arm * 26.f));
			AddPart(Cube, Out, FVector(4.f, 90.f, 24.f), Yaw, Basic, FLinearColor(0.03f, 0.15f, 0.55f));
			AddPart(Cube, Out + Yaw.RotateVector(FVector(2.5f, 0.f, 0.f)), FVector(1.f, 82.f, 16.f), Yaw, Basic, FLinearColor(0.95f, 0.95f, 0.93f));
			AddPart(Cube, Out + Yaw.RotateVector(FVector(-2.5f, 0.f, 0.f)), FVector(1.f, 82.f, 16.f), Yaw, Basic, FLinearColor(0.95f, 0.95f, 0.93f));
		}
		break;
	}
}

void AMishpachaStreetProp::UpdateLamps()
{
	if (Lamps.Num() < 3)
	{
		return;
	}
	// 24 s cycle on the server clock (same on every machine): green 10, yellow 2, red 12; the cross street is half a cycle later.
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float T = FMath::Fmod(static_cast<float>(Now) + Phase * 12.f, 24.f);
	const int32 Lit = State == EMishpachaPropState::Broken ? -1 : (T < 10.f ? 2 : (T < 12.f ? 1 : 0));
	for (int32 Lamp = 0; Lamp < 3; ++Lamp)
	{
		Lamps[Lamp]->SetScalarParameterValue(TEXT("Glow"), Lamp == Lit ? 18.f : 0.15f);
	}
}

void AMishpachaStreetProp::HitByVehicle(const FVector& VehicleVelocity)
{
	if (!HasAuthority() || State != EMishpachaPropState::Standing)
	{
		return;
	}
	const float Speed = VehicleVelocity.Size2D();
	if (Speed < 300.f)
	{
		return;
	}
	FlushNetDormancy();
	HitVelocity = VehicleVelocity;
	State = Speed > 1100.f ? EMishpachaPropState::Broken : EMishpachaPropState::Bent; // ~40 km/h snaps it off
	UE_LOG(LogMishpacha, Log, TEXT("Street prop %s %s at %.0f km/h"), *GetName(), State == EMishpachaPropState::Broken ? TEXT("broken") : TEXT("bent"), Speed * 0.036f);
	OnRep_State();
}

void AMishpachaStreetProp::OnRep_State()
{
	if (State == EMishpachaPropState::Standing || !Pole)
	{
		return;
	}
	FVector Direction = HitVelocity.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector();
	}

	if (State == EMishpachaPropState::Bent)
	{
		// Folded over at the foot, in the direction the car was going; out of the way for good.
		const float Angle = FMath::GetMappedRangeValueClamped(FVector2f(300.f, 1100.f), FVector2f(35.f, 80.f), HitVelocity.Size2D());
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
		Pivot->SetWorldRotation(FQuat(Axis, FMath::DegreesToRadians(Angle)) * GetActorQuat());
		Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	// Broken: the whole thing flies, tumbling, and lands on the street (ignores cars and people).
	UpdateLamps();
	Pole->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	Pole->SetCollisionResponseToAllChannels(ECR_Block);
	Pole->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Pole->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	Pole->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Pole->SetSimulatePhysics(true);
	Pole->SetMassOverrideInKg(NAME_None, 40.f);
	Pole->AddImpulse(Direction * HitVelocity.Size2D() * 0.7f + FVector(0.f, 0.f, 450.f), NAME_None, true);
	Pole->AddAngularImpulseInDegrees(FVector::CrossProduct(FVector::UpVector, Direction) * 400.f, NAME_None, true);
	SetLifeSpan(0.f);
}
