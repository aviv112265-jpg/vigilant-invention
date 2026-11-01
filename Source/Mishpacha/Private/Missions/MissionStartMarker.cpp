// MISHPACHA - world marker that starts a mission.
#include "Missions/MissionStartMarker.h"
#include "Missions/MissionDefinition.h"
#include "Missions/MissionSubsystem.h"
#include "Characters/MishpachaCharacter.h"
#include "Core/MishpachaPlayerController.h"
#include "Family/FamilySubsystem.h"
#include "Family/SiblingDefinition.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MishpachaMissions"

AMissionStartMarker::AMissionStartMarker()
{
	// Ticks only to turn the label toward the local camera (disabled on dedicated servers).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(200.f);
	Trigger->SetCollisionProfileName(FName(TEXT("OverlapAllDynamic")));
	RootComponent = Trigger;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Default look: a flat disc on the ground (engine content, always available). Blueprints can replace it.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Visual->SetStaticMesh(CylinderMesh.Object);
		Visual->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
		Visual->SetRelativeScale3D(FVector(3.f, 3.f, 0.05f));
	}

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Trigger);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 180.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetWorldSize(48.f);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Replicate so clients see markers spawned at runtime by the server.
	bReplicates = true;
}

void AMissionStartMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMissionStartMarker, MarkerColor);
	DOREPLIFETIME(AMissionStartMarker, MarkerText);
}

void AMissionStartMarker::SetMission(UMissionDefinition* InMission)
{
	Mission = InMission;
	if (HasActorBegunPlay())
	{
		ResolveAppearance();
		RefreshAvailability();
	}
}

void AMissionStartMarker::BeginPlay()
{
	Super::BeginPlay();

	if (UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn))
	{
		DiscMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Visual->SetMaterial(0, DiscMaterial);
	}
	SetActorTickEnabled(GetNetMode() != NM_DedicatedServer);

	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMissionStartMarker::HandleOverlap);
		if (UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Missions->OnMissionEnded.AddDynamic(this, &AMissionStartMarker::HandleMissionEnded);
		}
		ResolveAppearance();
		// The default family is created after the gray-box city spawns its markers - look again next tick.
		GetWorldTimerManager().SetTimerForNextTick(this, &AMissionStartMarker::ResolveAppearance);
	}
	OnRep_Appearance();
	RefreshAvailability();
}

void AMissionStartMarker::ResolveAppearance()
{
	if (!Mission)
	{
		return;
	}

	FText WhoText = LOCTEXT("AnyoneJob", "JOB");
	if (!Mission->RequiredSibling.IsNone())
	{
		WhoText = FText::FromName(Mission->RequiredSibling);
		const UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
		const int32 Index = Family ? Family->FindSiblingIndex(Mission->RequiredSibling) : INDEX_NONE;
		const USiblingDefinition* Definition = Index != INDEX_NONE ? Family->GetSiblingState(Index).Definition.Get() : nullptr;
		if (Definition)
		{
			MarkerColor = Definition->AccentColor;
			if (!Definition->DisplayName.IsEmpty())
			{
				WhoText = Definition->DisplayName;
			}
		}
	}
	MarkerText = FText::Format(LOCTEXT("MarkerLabel", "{0}: {1}"), FText::FromString(WhoText.ToString().ToUpper()), Mission->Title);
	OnRep_Appearance();
}

void AMissionStartMarker::OnRep_Appearance()
{
	if (DiscMaterial)
	{
		DiscMaterial->SetVectorParameterValue(FName(TEXT("Color")), MarkerColor);
	}
	Label->SetText(MarkerText);
	Label->SetTextRenderColor(MarkerColor.ToFColor(true));
}

void AMissionStartMarker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && PC->PlayerCameraManager && Label->IsVisible())
	{
		const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
		Label->SetWorldRotation(FRotator(0.f, ToCamera.Rotation().Yaw, 0.f));
	}
}

void AMissionStartMarker::HandleOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	const APawn* OverlappingPawn = Cast<APawn>(OtherActor);
	APlayerController* PC = OverlappingPawn ? Cast<APlayerController>(OverlappingPawn->GetController()) : nullptr;
	UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>();
	if (!PC || !Mission || !Missions)
	{
		return;
	}

	if (Missions->CanStartMission(Mission, PC))
	{
		Missions->StartMission(Mission, PC);
		return;
	}

	// Explain why not, when it's the "wrong sibling" case.
	const AMishpachaCharacter* PlayerCharacter = Cast<AMishpachaCharacter>(OverlappingPawn);
	if (!Mission->RequiredSibling.IsNone() && (!PlayerCharacter || PlayerCharacter->GetSiblingId() != Mission->RequiredSibling))
	{
		if (AMishpachaPlayerController* MPC = Cast<AMishpachaPlayerController>(PC))
		{
			const UFamilySubsystem* Family = GetWorld()->GetSubsystem<UFamilySubsystem>();
			const int32 Index = Family ? Family->FindSiblingIndex(Mission->RequiredSibling) : INDEX_NONE;
			MPC->ClientShowNotification(Index != INDEX_NONE
				? FText::Format(LOCTEXT("WrongSiblingKey", "This job is for {0}. Press {1} to switch."), FText::FromName(Mission->RequiredSibling), FText::AsNumber(Index + 1))
				: FText::Format(LOCTEXT("WrongSibling", "This job is for {0}."), FText::FromName(Mission->RequiredSibling)));
		}
	}
}

void AMissionStartMarker::HandleMissionEnded(FName MissionId, bool bSuccess)
{
	RefreshAvailability();
}

void AMissionStartMarker::RefreshAvailability()
{
	const UMissionSubsystem* Missions = GetWorld() ? GetWorld()->GetSubsystem<UMissionSubsystem>() : nullptr;
	const bool bDone = Mission && Missions && !Mission->bRepeatable && Missions->IsMissionCompleted(Mission->MissionId);
	SetActorHiddenInGame(bDone);
	SetActorEnableCollision(!bDone);
}

#undef LOCTEXT_NAMESPACE
