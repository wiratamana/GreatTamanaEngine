#include "AnimationPoseEvaluator.h"

#include "AppendBoneSolver.h"
#include "IkSolver.h"
#include "SkeletonPose.h"

namespace gte {

std::vector<Mat4> EvaluateAnimatedSkinningPose(
    const SkeletonData& skeleton, const ResolvedAnimationBinding& binding, float frame)
{
    std::vector<BoneLocalOffset> pose = SampleAnimationPose(binding, frame);
    SolveIkChains(skeleton, pose);
    ApplyAppendInheritance(skeleton, pose);
    return ComputeSkinningMatrices(skeleton, pose);
}

} // namespace gte
