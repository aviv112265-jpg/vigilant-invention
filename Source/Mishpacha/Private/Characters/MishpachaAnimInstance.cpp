// MISHPACHA - code-only animation graph for the Third Person mannequin.
#include "Characters/MishpachaAnimInstance.h"
#include "Characters/MishpachaCharacter.h"
#include "Characters/MishpachaWeapon.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace MishpachaAnim
{
	template <typename T>
	static T* Load(const TCHAR* Path)
	{
		T* Asset = LoadObject<T>(nullptr, *FString::Printf(TEXT("/Game/Characters/Mannequins/Anims/%s"), Path), nullptr, LOAD_Quiet | LOAD_NoWarn);
		if (Asset)
		{
			Asset->AddToRoot();
		}
		return Asset;
	}

	/** Loaded once (game thread) and kept for the whole session. */
	struct FAssets
	{
		UBlendSpace* Locomotion = nullptr;
		UAnimSequence* Fall = nullptr;
		UAnimSequence* Pistol = nullptr;
		UAnimSequence* Rifle = nullptr;
		UAnimSequence* Strafe[4] = {};
	};

	static const FAssets& Get()
	{
		static FAssets Assets;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			Assets.Locomotion = Load<UBlendSpace>(TEXT("Unarmed/BS_Idle_Walk_Run.BS_Idle_Walk_Run"));
			Assets.Fall = Load<UAnimSequence>(TEXT("Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop"));
			Assets.Pistol = Load<UAnimSequence>(TEXT("Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS"));
			Assets.Rifle = Load<UAnimSequence>(TEXT("Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"));
			Assets.Strafe[0] = Load<UAnimSequence>(TEXT("Rifle/Jog/MF_Rifle_Jog_Fwd.MF_Rifle_Jog_Fwd"));
			Assets.Strafe[1] = Load<UAnimSequence>(TEXT("Rifle/Jog/MF_Rifle_Jog_Bwd.MF_Rifle_Jog_Bwd"));
			Assets.Strafe[2] = Load<UAnimSequence>(TEXT("Rifle/Jog/MF_Rifle_Jog_Left.MF_Rifle_Jog_Left"));
			Assets.Strafe[3] = Load<UAnimSequence>(TEXT("Rifle/Jog/MF_Rifle_Jog_Right.MF_Rifle_Jog_Right"));
		}
		return Assets;
	}
}

UMishpachaAnimInstance::UMishpachaAnimInstance()
{
}

bool UMishpachaAnimInstance::HasAnimations()
{
	const MishpachaAnim::FAssets& Assets = MishpachaAnim::Get();
	return Assets.Locomotion && Assets.Pistol && Assets.Rifle;
}

// ---------------------------------------------------------------------------
// Proxy: the node graph
// ---------------------------------------------------------------------------

void FMishpachaAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	const MishpachaAnim::FAssets& Assets = MishpachaAnim::Get();

	Locomotion.SetBlendSpace(Assets.Locomotion);
	Locomotion.SetLoop(true);

	StrafeBlend.Poses.Reset();
	StrafeBlend.DesiredAlphas.Reset();
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		Strafe[Dir].SetSequence(Assets.Strafe[Dir]);
		Strafe[Dir].SetLoopAnimation(true);
		StrafeBlend.AddPose();
		StrafeBlend.Poses[Dir].SetLinkNode(&Strafe[Dir]);
	}
	StrafeBlend.bNormalizeAlpha = true;
	StrafeOrForward.A.SetLinkNode(&Locomotion);
	StrafeOrForward.B.SetLinkNode(&StrafeBlend);

	Fall.SetSequence(Assets.Fall);
	Fall.SetLoopAnimation(true);
	AirBlend.A.SetLinkNode(&StrafeOrForward);
	AirBlend.B.SetLinkNode(&Fall);

	PistolHold.SetSequence(Assets.Pistol);
	PistolHold.SetLoopAnimation(true);
	RifleHold.SetSequence(Assets.Rifle);
	RifleHold.SetLoopAnimation(true);
	HoldBlend.A.SetLinkNode(&PistolHold);
	HoldBlend.B.SetLinkNode(&RifleHold);

	// Everything from the first spine bone up takes the weapon pose. Mesh-space rotations keep the
	// arms pointing where the body faces while the hips swing with the legs.
	UpperBody.BasePose.SetLinkNode(&AirBlend);
	UpperBody.BlendPoses.Reset();
	UpperBody.BlendWeights.Reset();
	UpperBody.LayerSetup.Reset();
	UpperBody.AddPose();
	UpperBody.BlendPoses[0].SetLinkNode(&HoldBlend);
	UpperBody.LayerSetup.SetNum(1);
	FBranchFilter& Filter = UpperBody.LayerSetup[0].BranchFilters.AddDefaulted_GetRef();
	Filter.BoneName = TEXT("spine_01");
	Filter.BlendDepth = 0;
	UpperBody.BlendWeights[0] = 0.f;
	UpperBody.bMeshSpaceRotationBlend = true;

	AimNode.Source.SetLinkNode(&UpperBody);

	FAnimInstanceProxy::Initialize(InAnimInstance);
}

void FMishpachaAnimInstanceProxy::GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes)
{
	OutNodes.Append({ &Locomotion, &Strafe[0], &Strafe[1], &Strafe[2], &Strafe[3], &StrafeBlend, &StrafeOrForward,
		&Fall, &AirBlend, &PistolHold, &RifleHold, &HoldBlend, &UpperBody, &AimNode });
}

void FMishpachaAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	const USkeletalMeshComponent* MeshComponent = GetSkelMeshComponent();
	const AMishpachaCharacter* Character = MeshComponent ? Cast<AMishpachaCharacter>(MeshComponent->GetOwner()) : nullptr;
	if (!Character)
	{
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	const FVector Velocity = Character->GetVelocity();
	const float GroundSpeed = Velocity.Size2D();
	const bool bInAir = Movement && Movement->IsFalling();
	const bool bFacingAim = Movement && !Movement->bOrientRotationToMovement;
	const AMishpachaWeapon* Weapon = Character->GetWeapon();
	const bool bArmed = Weapon && !Weapon->IsHidden();

	auto Approach = [DeltaSeconds](float& Value, float Target, float Speed) { Value = FMath::FInterpConstantTo(Value, Target, DeltaSeconds, Speed); };
	Approach(ArmedAlpha, bArmed ? 1.f : 0.f, 5.f);
	Approach(AirAlpha, bInAir ? 1.f : 0.f, 6.f);
	Approach(StrafeAlpha, (bFacingAim && GroundSpeed > 30.f) ? 1.f : 0.f, 5.f);

	Locomotion.SetPosition(FVector(GroundSpeed, 0.f, 0.f));
	AirBlend.Alpha = AirAlpha;
	StrafeOrForward.Alpha = StrafeAlpha;
	HoldBlend.Alpha = (Weapon && Weapon->IsTwoHanded()) ? 1.f : 0.f;
	UpperBody.BlendWeights[0] = ArmedAlpha;

	// Directional jogs while facing the crosshair (strafing): pick by the move direction relative to the body.
	if (GroundSpeed > 1.f)
	{
		const FVector Local = Character->GetActorRotation().UnrotateVector(Velocity).GetSafeNormal2D();
		StrafeBlend.DesiredAlphas[0] = FMath::Max(0.f, static_cast<float>(Local.X));
		StrafeBlend.DesiredAlphas[1] = FMath::Max(0.f, static_cast<float>(-Local.X));
		StrafeBlend.DesiredAlphas[2] = FMath::Max(0.f, static_cast<float>(-Local.Y));
		StrafeBlend.DesiredAlphas[3] = FMath::Max(0.f, static_cast<float>(Local.Y));
	}
	const float StrafeRate = FMath::Clamp(GroundSpeed / 400.f, 0.5f, 1.6f);
	for (FAnimNode_SequencePlayer_Standalone& Player : Strafe)
	{
		Player.SetPlayRate(StrafeRate);
	}

	// Aim pitch as a component-space rotation about the character's right axis.
	float Pitch = FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch);
	Pitch = FMath::Clamp(Pitch, -60.f, 60.f) * ArmedAlpha;
	const FVector Forward = Character->GetActorForwardVector();
	const FVector Aimed = FRotator(Pitch, Character->GetActorRotation().Yaw, 0.f).Vector();
	const FQuat WorldDelta = FQuat::FindBetweenNormals(Forward, Aimed);
	const FQuat ComponentRotation = MeshComponent->GetComponentQuat();
	AimNode.SpineRotation = ComponentRotation.Inverse() * WorldDelta * ComponentRotation;
}

// ---------------------------------------------------------------------------
// Aim pitch node
// ---------------------------------------------------------------------------

void FMishpachaAimPitchNode::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	Source.Initialize(Context);
}

void FMishpachaAimPitchNode::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	const FBoneContainer& Bones = Context.AnimInstanceProxy->GetRequiredBones();
	const FName Names[3] = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Names[Index]);
		Spine[Index] = MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	}
}

void FMishpachaAimPitchNode::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
}

void FMishpachaAimPitchNode::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	if (SpineRotation.Equals(FQuat::Identity, 1.e-4f) || !Spine[0].IsValid() || !Spine[1].IsValid() || !Spine[2].IsValid())
	{
		return;
	}

	// Rotation of spine_01's parent in component space.
	FCompactPose& Pose = Output.Pose;
	const FBoneContainer& Bones = Pose.GetBoneContainer();
	FQuat ParentCS = FQuat::Identity;
	for (FCompactPoseBoneIndex Bone = Bones.GetParentBoneIndex(Spine[0]); Bone.IsValid(); Bone = Bones.GetParentBoneIndex(Bone))
	{
		ParentCS = Pose[Bone].GetRotation() * ParentCS;
	}

	// A third of the rotation per bone: new local = parent^-1 * part * parent * local.
	const FQuat Part = FQuat::Slerp(FQuat::Identity, SpineRotation, 1.f / 3.f);
	for (const FCompactPoseBoneIndex& Bone : Spine)
	{
		FTransform& Local = Pose[Bone];
		const FQuat NewLocal = ParentCS.Inverse() * Part * ParentCS * Local.GetRotation();
		Local.SetRotation(NewLocal.GetNormalized());
		ParentCS = ParentCS * Local.GetRotation();
	}
}
