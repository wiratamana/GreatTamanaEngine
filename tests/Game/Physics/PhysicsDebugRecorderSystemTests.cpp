#include "PhysicsTestChainFixture.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte {
namespace {

using physicstest::BuildChainModel;
using physicstest::ChainScene;
using physicstest::kAnchorBone;

constexpr int kJointCount = 3;
constexpr int kUpdateCount = 8;

// Render delta of update `index`; distinct per update so recorded frames can be told apart.
double DeltaFor(int index)
{
    return (1.0 + index) / 600.0;
}

// A free chain under gravity with a moving anchor, so the pose changes on every update.
void RunScene(ChainScene& scene, int updates)
{
    GlobalPhysicsSettings& settings = scene.physics.GetGlobalPhysicsSettings();
    settings.gravity = Vec3(0.0f, -9.8f, 0.0f);
    settings.bendLimitsEnabled = false;
    for (int i = 0; i < updates; ++i) {
        scene.SetAnimatedAnchorX(0.1f * static_cast<float>(i));
        scene.physics.Update(scene.registry, DeltaFor(i));
    }
}

void ExpectIdenticalPoses(const std::vector<BoneLocalOffset>& a, const std::vector<BoneLocalOffset>& b)
{
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i].translation.x, b[i].translation.x) << "bone " << i;
        EXPECT_EQ(a[i].translation.y, b[i].translation.y) << "bone " << i;
        EXPECT_EQ(a[i].translation.z, b[i].translation.z) << "bone " << i;
        EXPECT_EQ(a[i].rotation.x, b[i].rotation.x) << "bone " << i;
        EXPECT_EQ(a[i].rotation.y, b[i].rotation.y) << "bone " << i;
        EXPECT_EQ(a[i].rotation.z, b[i].rotation.z) << "bone " << i;
        EXPECT_EQ(a[i].rotation.w, b[i].rotation.w) << "bone " << i;
    }
}

TEST(PhysicsDebugRecorderSystemTests, DisabledRecorderRecordsNothing)
{
    ChainScene scene(BuildChainModel(kJointCount, 1.0f));
    RunScene(scene, kUpdateCount);

    EXPECT_FALSE(scene.physics.GetDebugRecorder().Enabled());
    EXPECT_EQ(scene.physics.GetDebugRecorder().FramesRecorded(scene.entity), 0u);
    EXPECT_TRUE(scene.physics.GetDebugRecorder().Frames(scene.entity, 0).empty());
}

TEST(PhysicsDebugRecorderSystemTests, EnabledRecorderDoesNotChangeTheSimulatedPose)
{
    ChainScene plain(BuildChainModel(kJointCount, 1.0f));
    RunScene(plain, kUpdateCount);

    ChainScene recorded(BuildChainModel(kJointCount, 1.0f));
    recorded.physics.GetDebugRecorder().Configure(true, 5);
    RunScene(recorded, kUpdateCount);

    ExpectIdenticalPoses(plain.Pose().pose, recorded.Pose().pose);
}

TEST(PhysicsDebugRecorderSystemTests, WindowOfFiveKeepsTheNewestFiveUpdatesOldestFirst)
{
    ChainScene scene(BuildChainModel(kJointCount, 1.0f));
    scene.physics.GetDebugRecorder().Configure(true, 5);
    RunScene(scene, kUpdateCount);

    const std::vector<const ChainFrameSample*> frames = scene.physics.GetDebugRecorder().Frames(scene.entity, 0);
    ASSERT_EQ(frames.size(), 5u);
    for (std::size_t k = 0; k < frames.size(); ++k) {
        EXPECT_NEAR(frames[k]->renderDeltaSeconds, static_cast<float>(DeltaFor(static_cast<int>(3 + k))), 1e-7f);
        EXPECT_EQ(frames[k]->animated.size(), static_cast<std::size_t>(kJointCount));
        EXPECT_EQ(frames[k]->display.size(), static_cast<std::size_t>(kJointCount));
        EXPECT_EQ(frames[k]->fk.size(), static_cast<std::size_t>(kJointCount));
    }

    // The newest frame matches the pose that was written last.
    const ChainFrameSample& newest = *frames.back();
    for (int j = 0; j < kJointCount; ++j) {
        const Vec3 fk = scene.BoneWorld(kAnchorBone + 1 + j);
        EXPECT_NEAR(newest.fk[static_cast<std::size_t>(j)].x, fk.x, 1e-5f);
        EXPECT_NEAR(newest.fk[static_cast<std::size_t>(j)].y, fk.y, 1e-5f);
        EXPECT_NEAR(newest.fk[static_cast<std::size_t>(j)].z, fk.z, 1e-5f);
    }
    const Vec3 anchor = scene.BoneWorld(kAnchorBone);
    EXPECT_NEAR(newest.rootAnimWorld.x, anchor.x, 1e-5f);
}

} // namespace
} // namespace gte
