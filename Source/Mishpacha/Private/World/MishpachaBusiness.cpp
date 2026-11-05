// MISHPACHA - a business Rafi can buy.
#include "World/MishpachaBusiness.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"

namespace
{
	UStaticMesh* LoadShape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
	}

	FLinearColor StateColor(EMishpachaBusinessState State)
	{
		switch (State)
		{
		case EMishpachaBusinessState::Owned: return FLinearColor(0.95f, 0.65f, 0.08f);
		case EMishpachaBusinessState::UnderAttack: return FLinearColor(0.9f, 0.05f, 0.03f);
		default: return FLinearColor(0.15f, 0.7f, 0.3f);
		}
	}
}

AMishpachaBusiness::AMishpachaBusiness()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // on everyone's map

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	auto MakePart = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Root);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		return Part;
	};
	Disc = MakePart(TEXT("Disc"));
	Pole = MakePart(TEXT("Pole"));
	Sign = MakePart(TEXT("Sign"));
}

void AMishpachaBusiness::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMishpachaBusiness, BusinessId);
	DOREPLIFETIME(AMishpachaBusiness, DisplayName);
	DOREPLIFETIME(AMishpachaBusiness, Price);
	DOREPLIFETIME(AMishpachaBusiness, Income);
	DOREPLIFETIME(AMishpachaBusiness, bLost);
	DOREPLIFETIME(AMishpachaBusiness, AttackEndTime);
	DOREPLIFETIME(AMishpachaBusiness, State);
}

void AMishpachaBusiness::Setup(FName InId, const FText& InName, int32 InPrice, int32 InIncome)
{
	BusinessId = InId;
	DisplayName = InName;
	Price = InPrice;
	Income = InIncome;
}

void AMishpachaBusiness::BeginPlay()
{
	Super::BeginPlay();

	UStaticMesh* Cylinder = LoadShape(TEXT("Cylinder"));
	UStaticMesh* Cube = LoadShape(TEXT("Cube"));
	UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Mishpacha/Materials/M_CarLight.M_CarLight"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Cylinder || !Cube || !Basic)
	{
		return;
	}

	Disc->SetStaticMesh(Cylinder);
	Disc->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
	Disc->SetRelativeScale3D(FVector(4.f, 4.f, 0.04f));
	DiscMaterial = UMaterialInstanceDynamic::Create(Basic, this);
	Disc->SetMaterial(0, DiscMaterial);

	Pole->SetStaticMesh(Cylinder);
	Pole->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	Pole->SetRelativeScale3D(FVector(0.12f, 0.12f, 3.f));
	UMaterialInstanceDynamic* PoleMaterial = UMaterialInstanceDynamic::Create(Basic, this);
	PoleMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.1f, 0.1f, 0.11f));
	Pole->SetMaterial(0, PoleMaterial);

	Sign->SetStaticMesh(Cube);
	Sign->SetRelativeLocation(FVector(0.f, 0.f, 320.f));
	Sign->SetRelativeScale3D(FVector(0.15f, 1.6f, 0.8f));
	SignMaterial = UMaterialInstanceDynamic::Create(Glow ? Glow : Basic, this);
	Sign->SetMaterial(0, SignMaterial);

	OnRep_State();
}

void AMishpachaBusiness::SetState(EMishpachaBusinessState NewState, double InAttackEndTime)
{
	State = NewState;
	AttackEndTime = InAttackEndTime;
	OnRep_State();
}

void AMishpachaBusiness::OnRep_State()
{
	const FLinearColor Color = StateColor(State);
	if (DiscMaterial)
	{
		DiscMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (SignMaterial)
	{
		SignMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		SignMaterial->SetScalarParameterValue(TEXT("Glow"), 3.f);
	}
}

AMishpachaBusiness* AMishpachaBusiness::FindNearest(const UWorld* World, const FVector& Location, float Radius)
{
	AMishpachaBusiness* Best = nullptr;
	double BestDist = FMath::Square(static_cast<double>(Radius));
	for (TActorIterator<AMishpachaBusiness> It(World); It; ++It)
	{
		const double Dist = FVector::DistSquared2D(It->GetActorLocation(), Location);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = *It;
		}
	}
	return Best;
}
