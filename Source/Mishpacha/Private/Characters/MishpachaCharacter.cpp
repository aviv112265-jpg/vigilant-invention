// MISHPACHA - base playable / NPC human character.
#include "Characters/MishpachaCharacter.h"
#include "Characters/MishpachaWeapon.h"
#include "Characters/MishpachaAnimInstance.h"
#include "Characters/MishpachaOutfits.h"
#include "Math/RandomStream.h"
#include "Mishpacha.h"
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
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include <initializer_list>

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

	// Everyone carries a basic pistol until Blueprints assign real weapons.
	DefaultWeaponClass = AMishpachaWeapon::StaticClass();
	Loadout = { AMishpachaWeapon::StaticClass(), AMishpachaSMG::StaticClass(), AMishpachaShotgun::StaticClass(), AMishpachaRifle::StaticClass() };
}

void AMishpachaCharacter::BeginPlay()
{
	Super::BeginPlay();

	SetupAutoVisuals();
	if (GetNetMode() != NM_DedicatedServer && GetMesh())
	{
		BonesFinalizedHandle = GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(
			FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &AMishpachaCharacter::PlaceWeaponInHands));
	}

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
	DOREPLIFETIME(AMishpachaCharacter, DeathImpulse);
	DOREPLIFETIME_CONDITION(AMishpachaCharacter, bSprinting, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AMishpachaCharacter, bAiming, COND_SkipOwner);
	DOREPLIFETIME(AMishpachaCharacter, CurrentWeapon);
	DOREPLIFETIME(AMishpachaCharacter, EquippedSlot);
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
	// Aiming (or just fired): strafe and face the camera direction; otherwise turn toward movement.
	const bool bFaceCamera = bAiming || bFacingAim;
	GetCharacterMovement()->bOrientRotationToMovement = !bFaceCamera;
	bUseControllerRotationYaw = bFaceCamera;
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
	// Hip fire: turn to face the crosshair, and stay that way a moment after the last shot.
	bFacingAim = true;
	UpdateAimState();
	GetWorldTimerManager().ClearTimer(FacingAimTimer);
	CurrentWeapon->StartFire();
}

void AMishpachaCharacter::RequestEquipSlot(int32 Slot)
{
	if (Slot == EquippedSlot && (Slot == INDEX_NONE || CurrentWeapon))
	{
		return;
	}
	StopFire();
	if (HasAuthority())
	{
		EquipSlot(Slot);
	}
	else
	{
		ServerEquipSlot(Slot);
	}
}

void AMishpachaCharacter::ServerEquipSlot_Implementation(int32 Slot)
{
	EquipSlot(Slot);
}

void AMishpachaCharacter::EquipSlot(int32 Slot)
{
	if (!HasAuthority() || bDead)
	{
		return;
	}
	if (Slot == INDEX_NONE || !Loadout.IsValidIndex(Slot) || !Loadout[Slot])
	{
		// Holster: hands free.
		if (CurrentWeapon)
		{
			CurrentWeapon->Destroy();
			CurrentWeapon = nullptr;
		}
		EquippedSlot = INDEX_NONE;
		return;
	}
	EquippedSlot = Slot;
	EquipWeaponClass(Loadout[Slot]);
}

void AMishpachaCharacter::StopFire()
{
	if (CurrentWeapon)
	{
		CurrentWeapon->StopFire();
	}
	if (bFacingAim)
	{
		GetWorldTimerManager().SetTimer(FacingAimTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			bFacingAim = false;
			UpdateAimState();
		}), 1.2f, false);
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
		// The code-built gun is lined up with both hands every frame (PlaceWeaponInHands); a real
		// weapon mesh from a Blueprint snaps to the hand socket as usual.
		const bool bGraybox = !CurrentWeapon->GetWeaponMesh() || !CurrentWeapon->GetWeaponMesh()->GetSkeletalMeshAsset();
		CurrentWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, bGraybox ? NAME_None : WeaponSocketName);
		CurrentWeapon->SetActorHiddenInGame(CurrentVehicle != nullptr);
		PlaceWeaponInHands();
	}
}

void AMishpachaCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BonesFinalizedHandle.IsValid() && GetMesh())
	{
		GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(BonesFinalizedHandle);
		BonesFinalizedHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AMishpachaCharacter::PlaceWeaponInHands()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!CurrentWeapon || !Body || bDead || CurrentWeapon->IsHidden() || CurrentWeapon->GetAttachParentSocketName() != NAME_None)
	{
		return;
	}
	static const FName HandR(TEXT("hand_r"));
	static const FName FingerR(TEXT("middle_01_r"));
	static const FName HandL(TEXT("hand_l"));
	static const FName FingerL(TEXT("middle_01_l"));
	if (Body->GetBoneIndex(HandR) == INDEX_NONE || Body->GetBoneIndex(FingerR) == INDEX_NONE)
	{
		return;
	}

	// Grip = the middle of the right palm. Pistols point where the character aims; long guns run from
	// the right hand through the left one, the way the rifle pose holds them.
	const FVector RightPalm = (Body->GetBoneLocation(HandR) + Body->GetBoneLocation(FingerR)) * 0.5f;
	const FVector RightBack = Body->GetBoneLocation(HandR);
	FVector Forward = FRotator(FMath::Clamp(FRotator::NormalizeAxis(GetBaseAimRotation().Pitch), -60.f, 60.f), GetActorRotation().Yaw, 0.f).Vector();
	if (CurrentWeapon->IsTwoHanded() && Body->GetBoneIndex(HandL) != INDEX_NONE)
	{
		const FVector LeftPalm = (Body->GetBoneLocation(HandL) + Body->GetBoneLocation(FingerL)) * 0.5f;
		const FVector ToLeft = LeftPalm - RightPalm;
		if (ToLeft.Size() > 15.f && (ToLeft.GetSafeNormal() | Forward) > 0.3f)
		{
			Forward = ToLeft.GetSafeNormal();
		}
	}
	// Up: away from the back of the hand (the slide sits on top of the fist), kept square to the barrel.
	FVector Up = FVector::VectorPlaneProject(FVector::UpVector, Forward).GetSafeNormal();
	const FVector Palm = (RightPalm - RightBack).GetSafeNormal();
	if (!Palm.IsNearlyZero())
	{
		Up = FVector::VectorPlaneProject(Up * 2.f - Palm * 0.3f, Forward).GetSafeNormal();
	}
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
	CurrentWeapon->SetActorLocationAndRotation(RightPalm + Up * 1.5f, Rotation);
}

void AMishpachaCharacter::HitByVehicle(AActor* Vehicle, const FVector& VehicleVelocity, AController* Driver)
{
	if (!HasAuthority() || bDead || CurrentVehicle)
	{
		return;
	}
	const float Speed = VehicleVelocity.Size();
	if (Speed < 250.f)
	{
		return;
	}

	// Thrown forward and up, away from the bumper.
	const FVector Launch = VehicleVelocity * 0.9f + FVector(0.f, 0.f, 200.f + Speed * 0.25f);
	// ~25 km/h kills, slower bumps hurt.
	const float DamageAmount = Speed >= 700.f ? MaxHealth * 2.f : FMath::GetMappedRangeValueClamped(FVector2f(250.f, 700.f), FVector2f(15.f, 90.f), Speed);
	if (Health - DamageAmount <= 0.f)
	{
		DeathImpulse = Launch;
	}
	else
	{
		LaunchCharacter(Launch * 0.6f, true, true);
	}
	FDamageEvent DamageEvent;
	TakeDamage(DamageAmount, DamageEvent, Driver, Vehicle);
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
	UE_LOG(LogMishpacha, Log, TEXT("Died: %s (killed by %s)"), *GetName(), *GetNameSafe(Killer ? Killer->GetPawn() : nullptr));

	// Bring the player back from remote-controlled things (Omer's drone) so the death flow sees the right controller.
	if (USiblingAbilityComponent* Ability = GetAbility())
	{
		Ability->ForceEnd();
	}

	bDead = true;
	AController* MyController = GetController();

	if (Killer && Killer != MyController && Killer->IsPlayerController() && !ActorHasTag(FName(TEXT("Animal"))))
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
	if (GetMesh()->GetSkeletalMeshAsset())
	{
		GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
		GetMesh()->SetSimulatePhysics(true);
		if (!DeathImpulse.IsNearlyZero())
		{
			// Run over: the body flies off the bumper.
			GetMesh()->AddImpulseToAllBodiesBelow(DeathImpulse, NAME_None, true, true);
		}
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->SetActorHiddenInGame(true);
	}
	OnDeathVisuals();

	// Placeholder body: just fall over.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -70.f), FRotator(90.f, 0.f, 0.f));
	}
	if (PlaceholderHead)
	{
		PlaceholderHead->SetRelativeLocation(FVector(90.f, 0.f, -70.f));
	}

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

// ---------------------------------------------------------------------------
// Zero-setup visuals
// ---------------------------------------------------------------------------

namespace MishpachaVisuals
{
	template <typename T>
	static T* LoadFirst(std::initializer_list<const TCHAR*> Paths)
	{
		for (const TCHAR* Path : Paths)
		{
			if (T* Found = LoadObject<T>(nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static UClass* LoadFirstClass(std::initializer_list<const TCHAR*> Paths)
	{
		for (const TCHAR* Path : Paths)
		{
			if (UClass* Found = LoadClass<UAnimInstance>(nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn))
			{
				return Found;
			}
		}
		return nullptr;
	}
}

USkeletalMesh* AMishpachaCharacter::LoadMannequinMesh() const
{
	static const FName Shira(TEXT("Shira"));
	if (SiblingId == Shira || bFemaleBody)
	{
		return MishpachaVisuals::LoadFirst<USkeletalMesh>({
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn") });
	}
	return MishpachaVisuals::LoadFirst<USkeletalMesh>({
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny") });
}

void AMishpachaCharacter::SetSiblingId(FName NewSiblingId)
{
	SiblingId = NewSiblingId;
	OnRep_SiblingId();
}

void AMishpachaCharacter::OnRep_SiblingId()
{
	RefreshPlaceholderColor();

	// Siblings get their id right after spawning (after BeginPlay): swap Manny/Quinn if needed.
	if (bUsingAutoMannequin)
	{
		USkeletalMesh* Wanted = LoadMannequinMesh();
		if (Wanted && Wanted != GetMesh()->GetSkeletalMeshAsset())
		{
			GetMesh()->SetSkeletalMeshAsset(Wanted);
		}
		ApplyOutfit();
	}
}

void AMishpachaCharacter::ApplyOutfit()
{
	if (!bUsingAutoMannequin || !GetMesh())
	{
		return;
	}
	static const FName Shira(TEXT("Shira"));
	FMishpachaOutfit Outfit;
	if (!FMishpachaOutfit::ForSibling(SiblingId, Outfit))
	{
		// Same look on every machine: seeded from where the NPC appeared.
		const FVector Spot = GetActorLocation();
		FRandomStream Stream(FMath::FloorToInt32(Spot.X / 50.f) * 7919 + FMath::FloorToInt32(Spot.Y / 50.f) * 104729);
		if (ActorHasTag(FName(TEXT("Police"))))
		{
			Outfit = FMishpachaOutfit::Police();
		}
		else if (ActorHasTag(FName(TEXT("Rival"))))
		{
			Outfit = FMishpachaOutfit::Rival(Stream);
		}
		else if (ActorHasTag(FName(TEXT("Family"))))
		{
			Outfit = FMishpachaOutfit::FamilySoldier();
		}
		else
		{
			Outfit = FMishpachaOutfit::Civilian(Stream, bFemaleBody);
		}
	}
	Outfit.Apply(GetMesh(), SiblingId == Shira || bFemaleBody);
}

FLinearColor AMishpachaCharacter::GetPlaceholderColor() const
{
	static const FName Rafi(TEXT("Rafi"));
	static const FName Shira(TEXT("Shira"));
	static const FName Omer(TEXT("Omer"));

	if (SiblingId == Rafi)
	{
		return FLinearColor(0.85f, 0.35f, 0.08f); // burnt orange
	}
	if (SiblingId == Shira)
	{
		return FLinearColor(0.05f, 0.45f, 0.45f); // deep teal
	}
	if (SiblingId == Omer)
	{
		return FLinearColor(0.35f, 0.42f, 0.12f); // olive
	}
	if (PlaceholderColor.A > 0.f)
	{
		return FLinearColor(PlaceholderColor.R, PlaceholderColor.G, PlaceholderColor.B, 1.f);
	}
	return FLinearColor(0.6f, 0.6f, 0.65f);
}

void AMishpachaCharacter::SetupAutoVisuals()
{
	if (!bAutoVisuals || GetMesh()->GetSkeletalMeshAsset() || PlaceholderBody)
	{
		return;
	}

	// Pedestrians: about half of them women (same pick on every machine, see ApplyOutfit).
	if (ActorHasTag(FName(TEXT("Civilian"))))
	{
		const FVector Spot = GetActorLocation();
		FRandomStream Stream(FMath::FloorToInt32(Spot.X / 50.f) * 31 + FMath::FloorToInt32(Spot.Y / 50.f) * 17);
		bFemaleBody = Stream.FRand() < 0.45f;
	}

	// 1) Third Person template mannequin (Add Feature or Content Pack > Third Person). Paths differ between engine versions.
	USkeletalMesh* Mannequin = LoadMannequinMesh();
	// Code-built graph (proper weapon hold) when the template's pistol/rifle animations are there.
	UClass* AnimClass = UMishpachaAnimInstance::HasAnimations() ? UMishpachaAnimInstance::StaticClass() : MishpachaVisuals::LoadFirstClass({
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"),
		TEXT("/Game/Characters/Mannequins/Animations/ABP_Manny.ABP_Manny_C") });

	if (Mannequin && AnimClass)
	{
		GetMesh()->SetSkeletalMeshAsset(Mannequin);
		GetMesh()->SetAnimInstanceClass(AnimClass);
		bUsingAutoMannequin = true;
		ApplyOutfit();
		AttachWeapon();
		return;
	}

	// 2) Colored placeholder (engine basic shapes always exist).
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!Cylinder || !Sphere)
	{
		return;
	}

	if (BaseMaterial)
	{
		PlaceholderMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	}

	PlaceholderBody = NewObject<UStaticMeshComponent>(this, TEXT("PlaceholderBody"));
	PlaceholderBody->SetStaticMesh(Cylinder);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetupAttachment(GetCapsuleComponent());
	// Basic cylinder: 100 cm diameter, 100 cm tall, pivot at center.
	PlaceholderBody->SetRelativeLocation(FVector(0.f, 0.f, -18.f));
	PlaceholderBody->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.55f));
	PlaceholderBody->RegisterComponent();

	PlaceholderHead = NewObject<UStaticMeshComponent>(this, TEXT("PlaceholderHead"));
	PlaceholderHead->SetStaticMesh(Sphere);
	PlaceholderHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderHead->SetupAttachment(GetCapsuleComponent());
	PlaceholderHead->SetRelativeLocation(FVector(0.f, 0.f, 78.f));
	PlaceholderHead->SetRelativeScale3D(FVector(0.32f));
	PlaceholderHead->RegisterComponent();

	if (PlaceholderMaterial)
	{
		PlaceholderBody->SetMaterial(0, PlaceholderMaterial);
		PlaceholderHead->SetMaterial(0, PlaceholderMaterial);
	}
	RefreshPlaceholderColor();
}

void AMishpachaCharacter::RefreshPlaceholderColor()
{
	if (PlaceholderMaterial)
	{
		PlaceholderMaterial->SetVectorParameterValue(FName(TEXT("Color")), GetPlaceholderColor());
	}
}

#undef LOCTEXT_NAMESPACE
