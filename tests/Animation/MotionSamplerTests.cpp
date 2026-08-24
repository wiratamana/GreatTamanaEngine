#include "Animation/MotionSampler.h"

#include <gtest/gtest.h>

using namespace gte;

namespace {

BoneKeyframe MakeKeyframe(const std::string& name, std::uint32_t frame, const Vec3& translation)
{
    BoneKeyframe kf;
    kf.boneName = name;
    kf.frame = frame;
    kf.translation = translation;
    kf.rotation = Quat::Identity();
    return kf;
}

} // namespace

TEST(MotionSamplerTests, GroupAndSortBoneTracksByNameSortsEachBoneByFrame)
{
    MotionData motion;
    motion.boneKeyframes.push_back(MakeKeyframe("A", 10, Vec3{ 1.0f, 0.0f, 0.0f }));
    motion.boneKeyframes.push_back(MakeKeyframe("A", 0, Vec3::Zero()));
    motion.boneKeyframes.push_back(MakeKeyframe("B", 5, Vec3{ 0.0f, 1.0f, 0.0f }));

    const auto grouped = GroupAndSortBoneTracksByName(motion);

    ASSERT_EQ(grouped.size(), 2u);
    const auto& aTrack = grouped.at("A");
    ASSERT_EQ(aTrack.size(), 2u);
    EXPECT_EQ(aTrack[0].frame, 0u);
    EXPECT_EQ(aTrack[1].frame, 10u);
    EXPECT_EQ(grouped.at("B").size(), 1u);
}

// The core "bones/weights in the animation file and the model file don't
// necessarily match" scenario this engine is meant to handle gracefully -
// see ResolvedAnimationBinding's own doc comment (Animation/MotionSampler.h).
TEST(MotionSamplerTests, ResolveBoneTracksToSkeletonToleratesMismatchInBothDirections)
{
    SkeletonData skeleton;
    Bone a;
    a.name = "A";
    skeleton.bones.push_back(a);
    Bone b;
    b.name = "B"; // No matching motion track for this one at all.
    skeleton.bones.push_back(b);

    MotionData motion;
    motion.boneKeyframes.push_back(MakeKeyframe("A", 0, Vec3::Zero()));
    motion.boneKeyframes.push_back(MakeKeyframe("A", 20, Vec3{ 1.0f, 0.0f, 0.0f }));
    motion.boneKeyframes.push_back(MakeKeyframe("C", 50, Vec3::Zero())); // No matching skeleton bone at all.

    const ResolvedAnimationBinding binding = ResolveBoneTracksToSkeleton(skeleton, motion);

    ASSERT_EQ(binding.perBoneKeyframes.size(), 2u);
    EXPECT_EQ(binding.perBoneKeyframes[0].size(), 2u); // "A" - matched.
    EXPECT_TRUE(binding.perBoneKeyframes[1].empty()); // "B" - no match, stays at bind pose.
    // Reflects the motion's own real length, including the unmatched "C"
    // track's frame - the loop point is a property of the CLIP, not of
    // which bones happened to resolve.
    EXPECT_EQ(binding.lastFrame, 50u);
}

TEST(MotionSamplerTests, SampleBoneTrackReturnsIdentityForAnEmptyList)
{
    const BoneLocalOffset offset = SampleBoneTrack({}, 10.0f);
    EXPECT_TRUE(ApproximatelyEqual(offset.translation, Vec3::Zero()));
    EXPECT_TRUE(RepresentSameRotation(offset.rotation, Quat::Identity()));
}

TEST(MotionSamplerTests, SampleBoneTrackInterpolatesAndClampsAtTheEdges)
{
    std::vector<BoneKeyframe> track;
    track.push_back(MakeKeyframe("A", 0, Vec3{ 0.0f, 0.0f, 0.0f }));
    track.push_back(MakeKeyframe("A", 10, Vec3{ 10.0f, 0.0f, 0.0f }));

    const BoneLocalOffset midway = SampleBoneTrack(track, 5.0f);
    EXPECT_NEAR(midway.translation.x, 5.0f, 0.001f);

    const BoneLocalOffset beforeStart = SampleBoneTrack(track, -5.0f);
    EXPECT_NEAR(beforeStart.translation.x, 0.0f, 0.001f);

    const BoneLocalOffset afterEnd = SampleBoneTrack(track, 100.0f);
    EXPECT_NEAR(afterEnd.translation.x, 10.0f, 0.001f);
}

TEST(MotionSamplerTests, SampleAnimationPoseSamplesEveryBoneInTheBinding)
{
    ResolvedAnimationBinding binding;
    binding.perBoneKeyframes.resize(2);
    binding.perBoneKeyframes[0].push_back(MakeKeyframe("A", 0, Vec3{ 2.0f, 0.0f, 0.0f }));
    // perBoneKeyframes[1] stays empty - bind pose.

    const std::vector<BoneLocalOffset> pose = SampleAnimationPose(binding, 0.0f);
    ASSERT_EQ(pose.size(), 2u);
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3{ 2.0f, 0.0f, 0.0f }));
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()));
}
