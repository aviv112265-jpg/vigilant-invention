// MISHPACHA - code-only animation graph for the Third Person mannequin: walk/run legs, a proper
// two-handed weapon hold on the upper body and the spine bent toward the crosshair.
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"
#include "AnimNodes/AnimNode_MultiWayBlend.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "MishpachaAnimInstance.generated.h"

class UAnimSequence;
class UBlendSpace;

/** Bends spine_01..03 so the upper body (and the gun in its hands) follows the aim pitch. */
struct FMishpachaAimPitchNode : public FAnimNode_Base
{
	FPoseLink Source;
	/** Component-space rotation shared out over the three spine bones. */
	FQuat SpineRotation = FQuat::Identity;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;

private:
	FCompactPoseBoneIndex Spine[3] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
};

/**
 * Graph (built in code, no Blueprint):
 *   legs  = idle/walk/run blend space, or directional jogs while facing the aim, or the fall loop in the air
 *   upper = pistol or rifle "aim down sights" idle from spine_01 up (mesh-space rotations), when armed
 *   then the spine bends toward the aim pitch.
 */
struct FMishpachaAnimInstanceProxy : public FAnimInstanceProxy
{
	FMishpachaAnimInstanceProxy() = default;
	explicit FMishpachaAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual FAnimNode_Base* GetCustomRootNode() override { return &AimNode; }
	virtual void GetCustomNodes(TArray<FAnimNode_Base*>& OutNodes) override;

private:
	FAnimNode_BlendSpacePlayer_Standalone Locomotion;
	FAnimNode_SequencePlayer_Standalone Strafe[4]; // forward, backward, left, right
	FAnimNode_MultiWayBlend StrafeBlend;
	FAnimNode_TwoWayBlend StrafeOrForward;
	FAnimNode_SequencePlayer_Standalone Fall;
	FAnimNode_TwoWayBlend AirBlend;
	FAnimNode_SequencePlayer_Standalone PistolHold;
	FAnimNode_SequencePlayer_Standalone RifleHold;
	FAnimNode_TwoWayBlend HoldBlend;
	FAnimNode_LayeredBoneBlend UpperBody;
	FMishpachaAimPitchNode AimNode;

	float ArmedAlpha = 0.f;
	float AirAlpha = 0.f;
	float StrafeAlpha = 0.f;
};

UCLASS(Transient, NotBlueprintable)
class MISHPACHA_API UMishpachaAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UMishpachaAnimInstance();

	/** True if the template animations this graph needs are in the project. */
	static bool HasAnimations();

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FMishpachaAnimInstanceProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }
};
