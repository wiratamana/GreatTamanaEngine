#include "Game/Physics/PhysicsDebugRecorder.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace gte {
namespace {

constexpr Entity kEntity{ 1, 1 };
constexpr Entity kOtherEntity{ 2, 1 };

TEST(PhysicsDebugRecorderTests, DisabledRecorderStoresNothing)
{
    PhysicsDebugRecorder recorder;
    EXPECT_FALSE(recorder.Enabled());

    recorder.BeginFrame(kEntity, 1, { 3 }, 0.016f, 1);
    EXPECT_EQ(recorder.SlotFor(kEntity, 0), nullptr);
    EXPECT_EQ(recorder.FramesRecorded(kEntity), 0u);
    EXPECT_TRUE(recorder.Frames(kEntity, 0).empty());
}

TEST(PhysicsDebugRecorderTests, WindowOfFiveKeepsTheNewestFiveOldestFirst)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 5);

    for (int frame = 1; frame <= 8; ++frame) {
        recorder.BeginFrame(kEntity, 1, { 3 }, static_cast<float>(frame), frame);
        ChainFrameSample* slot = recorder.SlotFor(kEntity, 0);
        ASSERT_NE(slot, nullptr);
        ASSERT_EQ(slot->display.size(), 3u);
        slot->display[0] = Vec3(static_cast<float>(frame), 0.0f, 0.0f);
    }

    const std::vector<const ChainFrameSample*> frames = recorder.Frames(kEntity, 0);
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(recorder.FramesRecorded(kEntity), 5u);
    for (std::size_t k = 0; k < frames.size(); ++k) {
        const float expected = static_cast<float>(4 + k);
        EXPECT_FLOAT_EQ(frames[k]->renderDeltaSeconds, expected);
        EXPECT_FLOAT_EQ(frames[k]->display[0].x, expected);
        EXPECT_EQ(frames[k]->stepsThisFrame, static_cast<std::uint16_t>(4 + k));
    }
    EXPECT_EQ(frames.back(), recorder.SlotFor(kEntity, 0));
}

TEST(PhysicsDebugRecorderTests, ReusedSlotsStartWithZeroCounters)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 2);

    for (int frame = 0; frame < 2; ++frame) {
        recorder.BeginFrame(kEntity, 1, { 1 }, 0.01f, 1);
        ChainFrameSample* slot = recorder.SlotFor(kEntity, 0);
        slot->collisionPushedParticles = 7;
        slot->bendClampedJoints = 3;
        slot->nanGuardHits = 1;
    }
    recorder.BeginFrame(kEntity, 1, { 1 }, 0.01f, 1);
    const ChainFrameSample* reused = recorder.SlotFor(kEntity, 0);
    EXPECT_EQ(reused->collisionPushedParticles, 0u);
    EXPECT_EQ(reused->bendClampedJoints, 0u);
    EXPECT_EQ(reused->nanGuardHits, 0u);
}

TEST(PhysicsDebugRecorderTests, MemoryCapShrinksTheWindow)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 600);
    EXPECT_EQ(recorder.EffectiveWindowFrames(kEntity), 600u);

    for (int frame = 1; frame <= 400; ++frame) {
        recorder.BeginFrame(kEntity, 1, { 2000 }, static_cast<float>(frame), 1);
    }
    EXPECT_EQ(recorder.EffectiveWindowFrames(kEntity), 300u);

    const std::vector<const ChainFrameSample*> frames = recorder.Frames(kEntity, 0);
    ASSERT_EQ(frames.size(), 300u);
    EXPECT_FLOAT_EQ(frames.front()->renderDeltaSeconds, 101.0f);
    EXPECT_FLOAT_EQ(frames.back()->renderDeltaSeconds, 400.0f);
}

TEST(PhysicsDebugRecorderTests, WindowIsClampedToValidRange)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 0);
    EXPECT_EQ(recorder.RequestedWindowFrames(), 1u);
    recorder.Configure(true, 100000);
    EXPECT_EQ(recorder.RequestedWindowFrames(), 600u);
}

TEST(PhysicsDebugRecorderTests, EveryChainGetsItsOwnSizedSlot)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 4);
    recorder.BeginFrame(kEntity, 2, { 3, 5 }, 0.02f, 2);

    const ChainFrameSample* first = recorder.SlotFor(kEntity, 0);
    const ChainFrameSample* second = recorder.SlotFor(kEntity, 1);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(first->animated.size(), 3u);
    EXPECT_EQ(second->fk.size(), 5u);
    EXPECT_EQ(second->stepsThisFrame, 2u);

    EXPECT_EQ(recorder.SlotFor(kEntity, 2), nullptr);
    EXPECT_EQ(recorder.SlotFor(kOtherEntity, 0), nullptr);
}

TEST(PhysicsDebugRecorderTests, ChangingTheWindowClearsRecordedFrames)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 5);
    recorder.BeginFrame(kEntity, 1, { 1 }, 0.01f, 1);
    ASSERT_EQ(recorder.FramesRecorded(kEntity), 1u);

    recorder.Configure(true, 6);
    EXPECT_EQ(recorder.FramesRecorded(kEntity), 0u);
}

TEST(PhysicsDebugRecorderTests, DisablingDropsEverything)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 5);
    recorder.BeginFrame(kEntity, 1, { 1 }, 0.01f, 1);

    recorder.Configure(false, 5);
    EXPECT_EQ(recorder.SlotFor(kEntity, 0), nullptr);
    EXPECT_EQ(recorder.FramesRecorded(kEntity), 0u);
}

TEST(PhysicsDebugRecorderTests, ForgetAndClearDropRecordedEntities)
{
    PhysicsDebugRecorder recorder;
    recorder.Configure(true, 5);
    recorder.BeginFrame(kEntity, 1, { 1 }, 0.01f, 1);
    recorder.BeginFrame(kOtherEntity, 1, { 1 }, 0.01f, 1);

    recorder.Forget(kEntity);
    EXPECT_EQ(recorder.FramesRecorded(kEntity), 0u);
    EXPECT_EQ(recorder.FramesRecorded(kOtherEntity), 1u);

    recorder.Clear();
    EXPECT_EQ(recorder.FramesRecorded(kOtherEntity), 0u);
    EXPECT_TRUE(recorder.Enabled());
}

} // namespace
} // namespace gte
