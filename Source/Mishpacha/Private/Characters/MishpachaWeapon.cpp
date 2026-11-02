// MISHPACHA - simple server-authoritative hitscan weapon.
#include "Characters/MishpachaWeapon.h"
#include "Characters/MishpachaCharacter.h"
#include "World/MishpachaWorldEvents.h"
#include "World/MishpachaSounds.h"
#include "DrawDebugHelpers.h"
#include "Heat/CrimeLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MishpachaWeapon"

AMishpachaWeapon::AMishpachaWeapon()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Mesh);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Body->SetStaticMesh(CubeMesh.Object);
	}

	DamageTypeClass = UDamageType::StaticClass();
	DisplayName = LOCTEXT("Pistol", "Pistol");
}

AMishpachaSMG::AMishpachaSMG()
{
	DisplayName = LOCTEXT("SMG", "SMG");
	FireSoundName = TEXT("S_Shot_SMG");
	Damage = 14.f;
	FireRate = 11.f;
	bAutomatic = true;
	MagazineSize = 30;
	ReloadTime = 1.8f;
	SpreadDegrees = 4.f;
	Range = 6000.f;
	BodySize = FVector(34.f, 5.f, 14.f);
	bTwoHanded = true;
}

AMishpachaShotgun::AMishpachaShotgun()
{
	DisplayName = LOCTEXT("Shotgun", "Shotgun");
	FireSoundName = TEXT("S_Shot_Shotgun");
	Damage = 14.f;
	PelletsPerShot = 8;
	FireRate = 1.3f;
	MagazineSize = 6;
	ReloadTime = 2.4f;
	SpreadDegrees = 9.f;
	Range = 3000.f;
	BodySize = FVector(70.f, 6.f, 9.f);
	bTwoHanded = true;
}

AMishpachaRifle::AMishpachaRifle()
{
	DisplayName = LOCTEXT("Rifle", "Assault Rifle");
	FireSoundName = TEXT("S_Shot_Rifle");
	Damage = 30.f;
	FireRate = 8.f;
	bAutomatic = true;
	MagazineSize = 30;
	ReloadTime = 2.1f;
	SpreadDegrees = 1.8f;
	Range = 15000.f;
	BodySize = FVector(80.f, 6.f, 14.f);
	bTwoHanded = true;
}

void AMishpachaWeapon::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Ammo = MagazineSize;
	}

	if (Body && Body->GetStaticMesh() && !Mesh->GetSkeletalMeshAsset())
	{
		BuildGrayboxShape();
	}
}

void AMishpachaWeapon::BuildGrayboxShape()
{
	// Weapon space: +X along the barrel, origin in the right hand's grip; the character lines the
	// weapon up with its hands every frame (AMishpachaCharacter::PlaceWeaponInHands).
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInstanceDynamic* Metal = BaseMaterial ? UMaterialInstanceDynamic::Create(BaseMaterial, this) : nullptr;
	UMaterialInstanceDynamic* Polymer = BaseMaterial ? UMaterialInstanceDynamic::Create(BaseMaterial, this) : nullptr;
	if (Metal && Polymer)
	{
		Metal->SetVectorParameterValue(FName(TEXT("Color")), FLinearColor(0.035f, 0.036f, 0.04f));
		Polymer->SetVectorParameterValue(FName(TEXT("Color")), FLinearColor(0.012f, 0.012f, 0.012f));
	}

	// Box from its center and size in cm (cube = 100 cm, pivot at center).
	auto AddBox = [this](UStaticMeshComponent* Part, const FVector& Center, const FVector& Size, float Pitch, UMaterialInterface* Material)
	{
		if (!Part)
		{
			Part = NewObject<UStaticMeshComponent>(this);
			Part->SetStaticMesh(Body->GetStaticMesh());
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetCastShadow(false);
			Part->SetupAttachment(Mesh);
			Part->RegisterComponent();
			Parts.Add(Part);
		}
		Part->SetRelativeLocationAndRotation(Center, FRotator(Pitch, 0.f, 0.f));
		Part->SetRelativeScale3D(Size / 100.f);
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
	};

	const float L = BodySize.X;
	const float W = BodySize.Y;
	if (!bTwoHanded)
	{
		// Pistol: slide over the hand, frame, angled grip in the palm.
		AddBox(Body, FVector(5.f, 0.f, 5.2f), FVector(L * 0.9f, W * 0.8f, 3.6f), 0.f, Metal);
		AddBox(nullptr, FVector(4.f, 0.f, 2.6f), FVector(L * 0.75f, W * 0.7f, 2.4f), 0.f, Polymer);
		AddBox(nullptr, FVector(-1.f, 0.f, -2.f), FVector(3.4f, W * 0.7f, 10.f), -14.f, Polymer);
		return;
	}

	// Long guns: receiver, barrel, pistol grip; magazine and stock where they have one.
	const bool bShotgun = PelletsPerShot > 1;
	const float ReceiverFront = L * (bShotgun ? 0.32f : 0.4f);
	AddBox(Body, FVector((ReceiverFront - 8.f) * 0.5f, 0.f, 3.5f), FVector(ReceiverFront + 8.f, W * 0.8f, 7.f), 0.f, Metal);
	const float BarrelEnd = L * 0.72f;
	AddBox(nullptr, FVector((ReceiverFront + BarrelEnd) * 0.5f, 0.f, 4.5f), FVector(BarrelEnd - ReceiverFront, 2.4f, 2.4f), 0.f, Metal);
	AddBox(nullptr, FVector(-0.5f, 0.f, -3.5f), FVector(3.4f, W * 0.6f, 9.f), -16.f, Polymer);
	if (bShotgun)
	{
		// Magazine tube and pump under the barrel.
		AddBox(nullptr, FVector((ReceiverFront + BarrelEnd) * 0.5f, 0.f, 1.8f), FVector(BarrelEnd - ReceiverFront, 2.6f, 2.6f), 0.f, Metal);
		AddBox(nullptr, FVector(ReceiverFront + 10.f, 0.f, 1.8f), FVector(14.f, 4.2f, 4.2f), 0.f, Polymer);
	}
	else
	{
		AddBox(nullptr, FVector(ReceiverFront * 0.45f, 0.f, -4.5f), FVector(5.f, W * 0.5f, BodySize.Z), 12.f, Polymer);
		// Handguard around the barrel, where the left hand holds it.
		AddBox(nullptr, FVector(ReceiverFront + L * 0.12f, 0.f, 4.f), FVector(L * 0.24f, 4.6f, 5.2f), 0.f, Polymer);
	}
	if (L > 40.f)
	{
		const float StockLength = L * 0.3f;
		AddBox(nullptr, FVector(-8.f - StockLength * 0.5f, 0.f, 1.5f), FVector(StockLength, W * 0.7f, 8.f), 4.f, Polymer);
	}
}

void AMishpachaWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMishpachaWeapon, Ammo, COND_OwnerOnly);
}

void AMishpachaWeapon::StartFire()
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	// Semi-auto: don't let clicking faster than the fire rate eat shots on the server.
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bAutomatic && Now - LastLocalShotTime < 1.0 / FMath::Max(FireRate, 0.1f))
	{
		return;
	}

	FireOnce();
	if (bAutomatic)
	{
		GetWorldTimerManager().SetTimer(AutoFireTimer, this, &AMishpachaWeapon::FireOnce, 1.f / FMath::Max(FireRate, 0.1f), true);
	}
}

void AMishpachaWeapon::StopFire()
{
	GetWorldTimerManager().ClearTimer(AutoFireTimer);
}

void AMishpachaWeapon::FireOnce()
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AController* OwnerController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
	if (!OwnerController || Ammo <= 0 || bReloading)
	{
		StopFire();
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Single-bullet guns spread here; pellet guns send the aim line and the server spreads each pellet.
	// AI gunmen miss a lot more than players (they'd be laser-accurate otherwise).
	const float Spread = OwnerController->IsPlayerController() ? SpreadDegrees : SpreadDegrees * 2.f + AIExtraSpreadDegrees;
	const float HalfConeRadians = FMath::DegreesToRadians(Spread * 0.5f);
	const FVector Direction = PelletsPerShot > 1 ? ViewRotation.Vector() : FMath::VRandCone(ViewRotation.Vector(), HalfConeRadians);
	LastLocalShotTime = GetWorld()->GetTimeSeconds();
	ServerFire(ViewLocation, Direction);
}

void AMishpachaWeapon::ServerFire_Implementation(FVector_NetQuantize TraceStart, FVector_NetQuantizeNormal TraceDirection)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || Ammo <= 0 || bReloading)
	{
		return;
	}

	// Rate limit (with some tolerance for network jitter). AI gunmen also fire slower and hit softer.
	const AController* ShooterController = OwnerPawn->GetController();
	const bool bAI = !ShooterController || !ShooterController->IsPlayerController();
	const double MinInterval = bAI ? FMath::Max(1.0 / FMath::Max(FireRate, 0.1f), double(AIMinShotInterval)) : (1.0 / FMath::Max(FireRate, 0.1f)) * 0.75;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireTime < MinInterval)
	{
		return;
	}
	LastFireTime = Now;
	const float PelletDamage = bAI ? Damage * AIDamageScale : Damage;

	// Reject trace origins that are nowhere near the shooter (cheap anti-cheat).
	if (FVector::DistSquared(TraceStart, OwnerPawn->GetActorLocation()) > FMath::Square(800.0))
	{
		return;
	}

	--Ammo;
	OnRep_Ammo();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MishpachaWeaponTrace), true, this);
	Params.AddIgnoredActor(OwnerPawn);

	const FVector AimDirection = FVector(TraceDirection).GetSafeNormal();
	const float HalfConeRadians = FMath::DegreesToRadians(SpreadDegrees * 0.5f);
	bool bHitSomeone = false;
	bool bKilledSomeone = false;
	for (int32 Pellet = 0; Pellet < PelletsPerShot; ++Pellet)
	{
		const FVector Direction = PelletsPerShot > 1 ? FMath::VRandCone(AimDirection, HalfConeRadians) : AimDirection;
		const FVector TraceEnd = FVector(TraceStart) + Direction * Range;

		FHitResult Hit;
		// ECC_Pawn: blocked by world geometry and by character capsules.
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Pawn, Params);
		if (bHit && Hit.GetActor())
		{
			const AMishpachaCharacter* Victim = Cast<AMishpachaCharacter>(Hit.GetActor());
			const bool bWasAlive = Victim && !Victim->IsDead();
			UGameplayStatics::ApplyPointDamage(Hit.GetActor(), PelletDamage, Direction, Hit, OwnerPawn->GetController(), this, DamageTypeClass);
			if (bWasAlive)
			{
				bHitSomeone = true;
				bKilledSomeone |= Victim->IsDead();
			}
		}
		// One tracer per shot (the first pellet) keeps the effects cheap.
		if (Pellet == 0 || PelletsPerShot <= 3)
		{
			MulticastFireEffects(bHit ? FVector(Hit.ImpactPoint) : TraceEnd);
		}
	}
	if (bHitSomeone)
	{
		ClientConfirmHit(bKilledSomeone);
	}

	FMishpachaWorldEvents::OnGunshot().Broadcast(GetWorld(), OwnerPawn->GetActorLocation());

	// Gunshots attract heat (throttled so automatic fire doesn't spam witness checks).
	if (Now - LastCrimeReportTime > 2.0)
	{
		LastCrimeReportTime = Now;
		UCrimeLibrary::ReportCrimeByActor(OwnerPawn, ECrimeType::Gunshot, OwnerPawn->GetActorLocation());
	}

	if (Ammo <= 0)
	{
		bReloading = true;
		GetWorldTimerManager().SetTimer(ReloadTimer, this, &AMishpachaWeapon::FinishReload, FMath::Max(ReloadTime, 0.1f), false);
	}
}

void AMishpachaWeapon::ClientConfirmHit_Implementation(bool bKilled)
{
	LastConfirmedHitTime = GetWorld()->GetTimeSeconds();
	bLastHitWasKill = bKilled;
	MishpachaSounds::Play2D(this, TEXT("S_UI_HitMarker"), bKilled ? 0.7f : 0.45f, bKilled ? 0.8f : 1.f);
}

void AMishpachaWeapon::FinishReload()
{
	bReloading = false;
	Ammo = MagazineSize;
	OnRep_Ammo();
}

void AMishpachaWeapon::MulticastFireEffects_Implementation(FVector_NetQuantize ImpactPoint)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	BP_OnFired(ImpactPoint);
	MishpachaSounds::PlayAt(this, FireSoundName, Body ? Body->GetComponentLocation() : GetActorLocation(), 1.f, FMath::FRandRange(0.94f, 1.06f), 9000.f);

#if ENABLE_DRAW_DEBUG
	// Gray-box tracer so shots are visible before weapon meshes / VFX exist.
	if (bDrawDebugTracer)
	{
		const FVector Muzzle = Body ? Body->GetComponentLocation() : GetActorLocation();
		DrawDebugLine(GetWorld(), Muzzle, ImpactPoint, FColor(255, 220, 120), false, 0.08f, 0, 1.5f);
	}
#endif
}

void AMishpachaWeapon::OnRep_Ammo()
{
	// The owning client learns about the reload from the ammo count.
	if (!HasAuthority())
	{
		bReloading = Ammo <= 0;
	}
	BP_OnAmmoChanged(Ammo);
}

#undef LOCTEXT_NAMESPACE
