// MISHPACHA - drivable Chaos vehicle with enter/exit, theft reporting and a radio placeholder.
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "MishpachaVehicle.generated.h"

class AMishpachaCharacter;
class APlayerState;
class UCameraComponent;
class USpringArmComponent;

/**
 * Base class for every car, scooter, sherut and truck in Tel Avir.
 * Create Blueprint children (BP_SportsCar ...) and assign the skeletal mesh, animation blueprint
 * and wheel setup (e.g. copied from the Vehicle template's SportsCar).
 */
UCLASS()
class MISHPACHA_API AMishpachaVehicle : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	AMishpachaVehicle();

	virtual void Tick(float DeltaSeconds) override;

	/** Server: puts the character in the driver seat and gives their controller the vehicle. */
	bool EnterVehicle(AMishpachaCharacter* Character);

	/** Server: puts the driver back on foot next to the car. */
	void ExitVehicle();

	/** Owning client -> server. */
	void RequestExit();

	// Owning-client driving input (Chaos replicates inputs to the server itself).
	void SetThrottle(float Value);
	void SetBrake(float Value);
	void SetSteering(float Value);
	void SetHandbrake(bool bEngaged);
	void Look(const FVector2D& Axis);

	UFUNCTION(BlueprintCallable, Category = "Vehicle|Radio")
	void NextRadioStation();

	UFUNCTION(BlueprintPure, Category = "Vehicle|Radio")
	int32 GetRadioStation() const { return RadioStation; }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	AMishpachaCharacter* GetDriver() const { return Driver; }

	UFUNCTION(BlueprintPure, Category = "Vehicle")
	bool HasDriver() const { return Driver != nullptr; }

	FName GetDriverSocketName() const { return DriverSocketName; }

	/** True if this car has a mesh of its own, or the template vehicle (see TemplateVehicleClass) exists. */
	bool HasVehicleSetup() const;

	// ----- Lights -----

	/** Driver: switch the headlights off / back to automatic (they come on by themselves after dark). */
	UFUNCTION(BlueprintCallable, Category = "Vehicle|Lights")
	void ToggleLights();

	UFUNCTION(BlueprintPure, Category = "Vehicle|Lights")
	bool AreLightsOn() const { return bLightsOn; }

	bool AreLightsSwitchedOff() const { return bLightsSwitchedOff; }

	/** How far police can see this target (1 = normal): driving at night with the lights off - or on foot in the dark - is harder to spot. */
	static float GetPoliceSightScale(const AActor* Target);

	/** Server: repaint the body (any material with a "Paint Tint" parameter). Replicated. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Vehicle")
	void SetPaintColor(const FLinearColor& NewColor);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

	/** Engine loop (pitch follows speed) and the driver's radio (2D, local driver only). */
	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> EngineAudio;

	UPROPERTY(Transient)
	TObjectPtr<class UAudioComponent> RadioAudio;

	int32 PlayingRadioStation = 0;
	void UpdateSounds();

	/** Driver's choice: lights off even at night (replicated). */
	UPROPERTY(ReplicatedUsing = OnRep_LightsSwitchedOff)
	bool bLightsSwitchedOff = false;

	UFUNCTION()
	void OnRep_LightsSwitchedOff();

	UFUNCTION(Server, Reliable)
	void ServerToggleLights();

	/** Head lamps' beam (no shadows) and the glowing head / tail light blocks. */
	UPROPERTY(Transient)
	TObjectPtr<class USpotLightComponent> HeadBeam;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> HeadGlow;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> TailGlow;

	bool bLightsOn = false;
	bool bLightsBuilt = false;
	TWeakObjectPtr<class AMishpachaDayNight> DayNight;
	void BuildLights();
	void UpdateLights();

	/** Server: what the front (or rear, reversing) bumper is about to hit - people, cats, signs. */
	void CheckBumper(float DeltaSeconds);
	TMap<TWeakObjectPtr<AActor>, double> RecentBumps;

	/**
	 * Auto setup: a car with no skeletal mesh (the plain C++ class, e.g. the gray-box city's parked cars)
	 * copies its mesh, animation and handling from this vehicle Blueprint.
	 * Default: the sports car from the Vehicle content pack.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle|Auto Setup")
	TSoftClassPtr<AWheeledVehiclePawn> TemplateVehicleClass;

	/** Body paint (alpha 0 = keep the material's own color). */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_PaintColor, Category = "Vehicle")
	FLinearColor PaintColor = FLinearColor(0.f, 0.f, 0.f, 0.f);

	UFUNCTION()
	void OnRep_PaintColor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Socket on the vehicle mesh where the (hidden) driver is attached. Falls back to the mesh origin. */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FName DriverSocketName = TEXT("DriverSeat");

	/** Local-space exit point for the driver (Israel drives on the right -> driver door on the LEFT, negative Y). */
	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	FVector DriverExitOffset = FVector(0.f, -180.f, 40.f);

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float RearExitDistance = 380.f;

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle")
	float RoofExitHeight = 250.f;

	UPROPERTY(EditDefaultsOnly, Category = "Vehicle|Radio", meta = (ClampMin = "1"))
	int32 NumRadioStations = 5;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float LookSensitivity = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float LookRecenterDelay = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "Camera")
	float DefaultCameraPitch = -12.f;

	UPROPERTY(Replicated)
	TObjectPtr<AMishpachaCharacter> Driver;

	UPROPERTY(ReplicatedUsing = OnRep_RadioStation)
	int32 RadioStation = 0;

	UFUNCTION(Server, Reliable)
	void ServerRequestExit();

	UFUNCTION(Server, Reliable)
	void ServerNextRadioStation();

	UFUNCTION()
	void OnRep_RadioStation();

	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle")
	void BP_OnDriverChanged(AMishpachaCharacter* NewDriver);

	/** Hook up your radio audio here (station 0 = off). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Vehicle|Radio")
	void BP_OnRadioStationChanged(int32 NewStation);

private:
	/** Re-entering a car you were the last to drive is not theft. */
	TWeakObjectPtr<APlayerState> LastDriverPlayerState;

	float LookYaw = 0.f;
	float LookPitch = -12.f;
	float TimeSinceLookInput = 100.f;

	FVector FindExitLocation(const AMishpachaCharacter* Character) const;
	void ApplyTemplate(const AWheeledVehiclePawn* Template);
};
