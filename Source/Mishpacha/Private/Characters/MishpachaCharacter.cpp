// MISHPACHA - base playable / NPC human character.
#include "Characters/MishpachaCharacter.h"
#include "Characters/MishpachaWeapon.h"
#include "Vehicles/MishpachaVehicle.h"
#include "Abilities/SiblingAbilityComponent.h"
#include "Core/MishpachaGameModeBase.h"
#include "Core/MishpachaPlayerController.h"
#include "Heat/CrimeLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

#define LOCTEXT_NAMESPACE "MishpachaCharacter"

AMishpachaCharacter::AMishpachaCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	MoveComp->bOrientRotationToMovement = true;
	MoveComp->RotationRate = FRotator(0.f, 540.f, 0.f);
	MoveComp->JumpZVelocity = 500.f;
	MoveComp->AirControl = 0.35f;
	MoveComp->MaxWalkSpeed = JogSpeed;
	MoveComp->MinAnalogWalkSpeed = 20.f;
	MoveComp->BrakingDecelerationWalking = 2000.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = DefaultArmLength;
	CameraBoom->SocketOffset = FVector(0.f, 50.f, 60.f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Matches the UE5 Manny/Quinn skeletal mesh orientation.
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -96.f), FRotator(0.f, -90.f, 0.f));

	Health = MaxHealth;
}

void AMishpachaCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		Health = MaxHealth;
		if (DefaultWeaponClass && !CurrentWeapon)
		{
			EquipWeaponClass(DefaultWeaponClass);
		}
	}
	UpdateAimState();
}

void AMishpachaCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AMishpachaCharacter, SiblingId);
	DOREPLIFETIME(AMishpachaCharacter, Health);
	DOREPLIFETIME(AMishpachaCharacter, bDead);
	DOREPLIFETIME_CONDITION(AMishpachaCharacter, bSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AMishpachaCharacter, bAiming, COND_SkipOwner);
	DOREPLIFETIME(AMishpachaCharacter, CurrentWeapon);
	DOREPLIFETIME(AMishpachaCharacter, CurrentVehicle);
}

// ---------------------------------------------------------------------------
// Movement / camera
// ---------------------------------------------------------------------------

void AMishpachaCharacter::Move(const FVector2D& Axis)
{
	if (!Controller || bDead || CurrentVehicle)
	{
		return;
	}

	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(Forward, Axis.Y);
	AddMovementInput(Right, Axis.X);
}

void AMishpachaCharacter::Look(const FVector2D& Axis)
{
	if (!Controller)
	{
		return;
	}
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void AMishpachaCharacter::SetSprinting(bool bNewSprinting)
{
	bSprinting = bNewSprinting;
	UpdateMovementSpeed();
	if (!HasAuthority())
	{
		ServerSetSprinting(bNewSprinting);
	}
}

void AMishpachaCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	bSprinting = bNewSprinting;
	UpdateMovementSpeed();
}

void AMishpachaCharacter::SetAiming(bool bNewAiming)
{
	bAiming = bNewAiming && !CurrentVehicle && !bDead;
	UpdateAimState();
	if (!HasAuthority())
	{
		ServerSetAiming(bAiming);
	}
}

void AMishpachaCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
	bAiming = bNewAiming && !CurrentVehicle && !bDead;
	UpdateAimState();
}

void AMishpachaCharacter::OnRep_Sprinting()
{
	UpdateMovementSpeed();
}

void AMishpachaCharacter::OnRep_Aiming()
{
	UpdateAimState();
}

void AMishpachaCharacter::UpdateMovementSpeed()
{
	GetCharacterMovement()->MaxWalkSpeed = bAiming ? WalkSpeed : (bSprinting ? SprintSpeed : JogSpeed);
}

void AMishpachaCharacter::UpdateAimState()
{
	// Aiming: strafe and face the camera direction; otherwise turn toward movement.
	GetCharacterMovement()->bOrientRotationToMovement = !bAiming;
	bUseControllerRotationYaw = bAiming;
	if (CameraBoom)
	{
		CameraBoom->TargetArmLength = bAiming ? AimArmLength : DefaultArmLength;
	}
	UpdateMovementSpeed();
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

void AMishpachaCharacter::StartFire()
{
	if (bDead || CurrentVehicle || !CurrentWeapon)
	{
		return;
	}
	CurrentWeapon->StartFire();
}

void AMishpachaCharacter::StopFire()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire();
	}
}

void AMishpachaCharacter::EquipWeaponClass(TSubclassOf<AMishpachaWeapon> WeaponClass)
{
	if (!HasAuthority() || !WeaponClass)
	{
		return;
	}

	if (CurrentWeapon)
	{
		CurrentWeapon->Destroy();
		CurrentWeapon = nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.Instigator = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CurrentWeapon = GetWorld()->SpawnActor<AMishpachaWeapon>(WeaponClass, GetActorTransform(), Params);
	AttachWeapon();
}

void AMishpachaCharacter::OnRep_CurrentWeapon()
{
	AttachWeapon();
}

void AMishpachaCharacter::AttachWeapon()
{
	if (CurrentWeapon && GetMesh())
	{
		CurrentWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponSocketName);
		CurrentWeapon->SetActorHiddenInGame(CurrentVehicle != nullptr);
	}
}

float AMishpachaCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bDead)
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied <= 0.f)
	{
		return 0.f;
	}

	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
	OnRep_Health();

	const bool bByAnotherPlayer = EventInstigator && EventInstigator->IsPlayerController() && EventInstigator != GetController();
	if (bByAnotherPlayer && Health > 0.f)
	{
		UCrimeLibrary::ReportCrimeByActor(EventInstigator->GetPawn(), ECrimeType::Assault, GetActorLocation());
	}

	if (Health <= 0.f)
	{
		Die(EventInstigator);
	}
	return Applied;
}

void AMishpachaCharacter::OnRep_Health()
{
	OnHealthChanged.Broadcast(Health, MaxHealth);
}

void AMishpachaCharacter::Die(AController* Killer)
{
	if (bDead)
	{
		return;
	}

	// Bring the player back from remote-controlled things (Omer's drone) so the death flow sees the right controller.
	if (USiblingAbilityComponent* Ability = GetAbility())
	{
		Ability->ForceEnd();
	}

	bDead = true;
	AController* MyController = GetController();

	if (Killer && Killer != MyController && Killer->IsPlayerController())
	{
		const ECrimeType Crime = ActorHasTag(FName(TEXT("Police"))) ? ECrimeType::CopKilled : ECrimeType::Murder;
		UCrimeLibrary::ReportCrimeByActor(Killer->GetPawn(), Crime, GetActorLocation());
	}

	StopFire();

	if (AMishpachaGameModeBase* GameMode = GetWorld()->GetAuthGameMode<AMishpachaGameModeBase>())
	{
		GameMode->OnCharacterDied(this, MyController, Killer);
	}

	DetachFromControllerPendingDestroy();
	if (MyController && !MyController->IsPlayerController())
	{
		MyController->Destroy();
	}

	OnRep_Dead();
	SetLifeSpan(BodyLifeSpan);
	if (CurrentWeapon)
	{
		CurrentWeapon->SetLifeSpan(BodyLifeSpan);
	}
}

void AMishpachaCharacter::OnRep_Dead()
{
	if (!bDead)
	{
		return;
	}

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Simple ragdoll (requires a Physics Asset on the mesh).
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetSimulatePhysics(true);

	BP_OnDied();
}

// ---------------------------------------------------------------------------
// Interaction / vehicles / abilities
// ---------------------------------------------------------------------------

void AMishpachaCharacter::Interact()
{
	if (HasAuthority())
	{
		ServerInteract_Implementation();
	}
	else
	{
		ServerInteract();
	}
}

void AMishpachaCharacter::ServerInteract_Implementation()
{
	if (bDead)
	{
		return;
	}

	if (CurrentVehicle)
	{
		CurrentVehicle->ExitVehicle();
		return;
	}

	if (AMishpachaVehicle* Vehicle = FindNearbyVehicle())
	{
		Vehicle->EnterVehicle(this);
	}
}

AMishpachaVehicle* AMishpachaCharacter::FindNearbyVehicle() const
{
	AMishpachaVehicle* Best = nullptr;
	double BestDistSq = FMath::Square(static_cast<double>(InteractRadius));

	for (TActorIterator<AMishpachaVehicle> It(GetWorld()); It; ++It)
	{
		AMishpachaVehicle* Candidate = *It;
		if (!Candidate || Candidate->HasDriver())
		{
			continue;
		}

		const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

void AMishpachaCharacter::OnEnteredVehicle(AMishpachaVehicle* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}

	StopFire();
	bSprinting = false;
	bAiming = false;
	UpdateAimState();

	CurrentVehicle = Vehicle;
	ApplyVehicleState();
	AttachToComponent(Vehicle->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Vehicle->GetDriverSocketName());
}

void AMishpachaCharacter::OnExitedVehicle(const FVector& ExitLocation, const FRotator& ExitRotation)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocationAndRotation(ExitLocation, ExitRotation, false, nullptr, ETeleportType::TeleportPhysics);
	CurrentVehicle = nullptr;
	ApplyVehicleState();
}

void AMishpachaCharacter::OnRep_CurrentVehicle()
{
	ApplyVehicleState();
}

void AMishpachaCharacter::ApplyVehicleState()
{
	const bool bInVehicle = CurrentVehicle != nullptr;
	SetActorHiddenInGame(bInVehicle);
	SetActorEnableCollision(!bInVehicle);

	if (bInVehicle)
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}
	else if (!bDead)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}

	if (CurrentWeapon)
	{
		CurrentWeapon->SetActorHiddenInGame(bInVehicle);
	}
}

USiblingAbilityComponent* AMishpachaCharacter::GetAbility() const
{
	return FindComponentByClass<USiblingAbilityComponent>();
}

void AMishpachaCharacter::EnsureAbility(TSubclassOf<USiblingAbilityComponent> AbilityClass)
{
	if (!HasAuthority() || !AbilityClass || GetAbility())
	{
		return;
	}

	USiblingAbilityComponent* NewAbility = NewObject<USiblingAbilityComponent>(this, AbilityClass);
	NewAbility->SetIsReplicated(true);
	NewAbility->RegisterComponent();
	AddInstanceComponent(NewAbility);
}

void AMishpachaCharacter::UseAbility()
{
	if (bDead)
	{
		return;
	}

	if (USiblingAbilityComponent* Ability = GetAbility())
	{
		Ability->TryActivate();
	}
	else if (AMishpachaPlayerController* PC = Cast<AMishpachaPlayerController>(GetController()))
	{
		PC->ClientShowNotification(LOCTEXT("NoAbility", "This character has no special ability."));
	}
}

#undef LOCTEXT_NAMESPACE
