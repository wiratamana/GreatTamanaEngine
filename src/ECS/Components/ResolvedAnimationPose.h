#pragma once
#include "../../Animation/BoneLocalOffset.h"

#include <vector>

namespace gte {

// The ENTIRE hand-off contract between three independent stages
// Game::Update() calls in a fixed order every frame (see
// task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md's
// own v3 Revision Notice):
//
//   AnimationSystem::EvaluatePoses()  -> writes `pose` wholesale (sample ->
//                                        IK -> append -> FK-ready
//                                        BoneLocalOffset array). ZERO
//                                        knowledge that PhysicsSystem exists.
//   PhysicsSystem::Update()           -> OPTIONALLY overwrites individual
//                                        elements of `pose`, IN PLACE, for
//                                        whichever bones its own detected
//                                        dynamic chains simulate (see
//                                        Physics/BoneChainPhysicsResolver.h's
//                                        ApplyDynamicChainPhysicsToPose()).
//                                        Never replaces `pose` wholesale.
//   AnimationSystem::SkinAndUpload()  -> ONLY ever READS whatever `pose`
//                                        currently holds - does not, and
//                                        must not, care whether physics
//                                        touched it.
//
// Written EXCLUSIVELY by AnimationSystem::EvaluatePoses() (always
// OVERWRITING `pose` wholesale, never reading a previous frame's leftover
// value first) - attached to the SAME entity SkeletalAnimator/DynamicChainRig
// live on (a model's ROOT/animator entity - see MeshAssetSource.h/
// SkeletalAnimator.h/DynamicChainRig.h). This component IS the entire
// coupling surface between the three stages above - no stage calls another
// stage's code directly, and no stage holds a pointer/reference into another
// stage's private cache.
struct ResolvedAnimationPose {
    std::vector<BoneLocalOffset> pose;
};

} // namespace gte
