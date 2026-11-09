// Tier-1 tests: Game::Update() writes GPU-visible memory only for a frame slot it was given.
// No Renderer or GPU device is involved: a Game default-constructs without one.

#include "Core/EngineContext.h"
#include "Game/Game.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(GameUpdateGpuFrameTest, TheDefaultContextIsASynchronousFrame)
{
    const EngineContext context;
    ASSERT_TRUE(context.gpuFrame.has_value());
    EXPECT_FALSE(context.gpuFrame->IsAsync());
    EXPECT_EQ(context.gpuFrame->slot, 0u);
}

TEST(GameUpdateGpuFrameTest, AnUpdateWithoutAFreeSlotWritesNothingAndRequestsNoDispatch)
{
    Game game;
    EngineContext context;
    context.time.Advance(1.0 / 60.0, /*isPaused=*/false, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
    context.gpuFrame.reset();

    game.Update(context, InputState{});

    EXPECT_TRUE(game.CollectGpuSkinningDispatchRequests().empty());
}

TEST(GameUpdateGpuFrameTest, AnUpdateInAnOverlappedSlotRunsAndRequestsNothingWithoutSkinnedModels)
{
    Game game;
    EngineContext context;
    context.time.Advance(1.0 / 60.0, /*isPaused=*/false, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
    context.gpuFrame = OffscreenFrameTicket{ 1, 2, 0 };

    game.Update(context, InputState{});

    EXPECT_TRUE(game.CollectGpuSkinningDispatchRequests(*context.gpuFrame).empty());
}

TEST(GameUpdateGpuFrameTest, AFrozenFrameInAnOverlappedSlotKeepsTheRingsCurrentWithoutADispatch)
{
    Game game;
    EngineContext context;
    context.time.Advance(1.0 / 60.0, /*isPaused=*/true, /*isSteppedThisFrame=*/false, 1.0 / 60.0);
    ASSERT_TRUE(context.time.IsFrozenThisFrame());
    context.gpuFrame = OffscreenFrameTicket{ 0, 3, 1 };

    game.Update(context, InputState{});

    EXPECT_TRUE(game.CollectGpuSkinningDispatchRequests(*context.gpuFrame).empty());
}

} // namespace
} // namespace gte
