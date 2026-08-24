#pragma once

#include "../Math/Quat.h"
#include "../Math/Vec3.h"

namespace gte {

// One bone's local animation OFFSET at a particular instant in time - ADDED
// ON TOP of that bone's own bind-pose local transform (see SkeletonPose.h's
// ComputeSkinningMatrices()), never an absolute model-space transform on
// its own. Identity() (zero translation, identity rotation - i.e. this
// struct's own default-constructed value) is exactly the correct value for
// a bone that isn't driven by whatever motion is currently playing - it
// just stays at its authored bind pose (see MotionSampler.h's own doc
// comment for why this is expected to legitimately happen whenever a
// motion doesn't cover every bone in a model's own skeleton, or vice
// versa - the "bones/weights don't match between the animation file and
// the model file" problem this whole Animation/ module exists to handle
// gracefully).
struct BoneLocalOffset {
    Vec3 translation = Vec3::Zero();
    Quat rotation = Quat::Identity();
};

} // namespace gte
