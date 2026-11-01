// MISHPACHA - simple server-authoritative hitscan weapon.
#include "Characters/MishpachaWeapon.h"
#include "Heat/CrimeLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AMishpachaWeapon::AMishpachaWeapon()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;

	DamageTypeClass = UDamageType::StaticClass();
}

void AMishpachaWeapon::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Ammo = MagazineSize;
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
	if (!OwnerController || Ammo <= 0)
	{
		StopFire();
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const float HalfConeRadians = FMath::DegreesToRadians(SpreadDegrees * 0.5f);
	const FVector Direction = FMath::VRandCone(ViewRotation.Vector(), HalfConeRadians);
	ServerFire(ViewLocation, Direction);
}

void AMishpachaWeapon::ServerFire_Implementation(FVector_NetQuantize TraceStart, FVector_NetQuantizeNormal TraceDirection)
{
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || Ammo <= 0 || bReloading)
	{
		return;
	}

	// Rate limit (with some tolerance for network jitter).
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireTime < (1.0 / FMath::Max(FireRate, 0.1f)) * 0.75)
	{
		return;
	}
	LastFireTime = Now;

	// Reject trace origins that are nowhere near the shooter (cheap anti-cheat).
	if (FVector::DistSquared(TraceStart, OwnerPawn->GetActorLocation()) > FMath::Square(800.0))
	{
		return;
	}

	const FVector Direction = FVector(TraceDirection).GetSafeNormal();
	const FVector TraceEnd = FVector(TraceStart) + Direction * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MishpachaWeaponTrace), true, this);
	Params.AddIgnoredActor(OwnerPawn);

	FHitResult Hit;
	// ECC_Pawn: blocked by world geometry and by character capsules.
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Pawn, Params);

	--Ammo;
	OnRep_Ammo();

	if (bHit && Hit.GetActor())
	{
		UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Damage, Direction, Hit, OwnerPawn->GetController(), this, DamageTypeClass);
	}

	MulticastFireEffects(bHit ? FVector(Hit.ImpactPoint) : TraceEnd);

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
}

void AMishpachaWeapon::OnRep_Ammo()
{
	BP_OnAmmoChanged(Ammo);
}
