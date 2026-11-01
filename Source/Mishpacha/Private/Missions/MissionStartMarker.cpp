// MISHPACHA - world marker that starts a mission.
#include "Missions/MissionStartMarker.h"
#include "Missions/MissionDefinition.h"
#include "Missions/MissionSubsystem.h"
#include "Characters/MishpachaCharacter.h"
#include "Core/MishpachaPlayerController.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "MishpachaMissions"

AMissionStartMarker::AMissionStartMarker()
{
	PrimaryActorTick.bCanEverTick = false;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(200.f);
	Trigger->SetCollisionProfileName(FName(TEXT("OverlapAllDynamic")));
	RootComponent = Trigger;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AMissionStartMarker::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &AMissionStartMarker::HandleOverlap);
		if (UMissionSubsystem* Missions = GetWorld()->GetSubsystem<UMissionSubsystem>())
		{
			Missions->OnMissionEnded.AddDynamic(this, &AMissionStartMarker::HandleMissionEnded);
		}
	}
	RefreshAvailability();
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
			MPC->ClientShowNotification(FText::Format(LOCTEXT("WrongSibling", "This job is for {0}."), FText::FromName(Mission->RequiredSibling)));
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
