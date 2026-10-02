// Tier-1 test for better-render-pass-5 effort, BLOCK 3 - an in-process
// proof of the per-view Blackboard qualification guarantee
// (task_manager/better-render-pass-5/PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #4), WITHOUT needing a live Vulkan device, Core, or
// RenderFeatureCompositor - pure rg::RenderPipeline/RenderGraphBuilder/
// RenderPassBlackboard usage, mirroring RenderPipelineTests.cpp's own
// established fixture pattern (ProviderScope::PerActiveView driven
// across a hand-built RenderPassFrameContext whose activeViews lists
// more than one entry).
//
// Shape: a "PreOpaqueFeatures"-like producer provider writes a distinct
// marker value (an int tag, 1 for Game / 2 for Scene) into the
// Blackboard under a key qualified by frame.currentView, mirroring
// Core.cpp's real production provider (PHASE3) exactly (same
// ProviderScope::PerActiveView, same per-view key derivation idiom). A
// "RenderOpaque"-like standing-in reader provider, registered
// separately, Fetches that SAME per-view key and records what it
// actually got into a small, test-local, per-view result map. After
// DeclareInto() completes for BOTH views, this test asserts the reader
// saw EXACTLY the tag matching ITS OWN view, for BOTH views - proving
// no cross-view aliasing happened.

#include "Renderer/RenderGraph/RenderPipeline.h"

#include <gtest/gtest.h>

#include <unordered_map>

namespace gte::rg {
namespace {

constexpr RenderPassId kMarkerGameKey = "Test.PreOpaqueMarker.Game"_passId;
constexpr RenderPassId kMarkerSceneKey = "Test.PreOpaqueMarker.Scene"_passId;

void NoOpExecute(PassContext&) { }

} // namespace

TEST(PreOpaqueRenderPipelineBlackboardQualificationTest,
    EachViewsReaderFetchesExactlyItsOwnViewsPublishedValueNeverTheOtherViews)
{
    RenderPipeline pipeline;
    std::unordered_map<std::uint64_t, int> fetchedByView; // keyed by a stable per-view int derived below.

    // Mirrors Core.cpp's real "PreOpaqueFeatures" provider shape: a
    // PerActiveView provider that Publish()es under a per-view key,
    // using the SAME dual-key idiom "AtmosphereViewLut" already ships.
    pipeline.Register("PreOpaqueFeatures_Stub", ProviderScope::PerActiveView,
        [](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const RenderPassId key = isGameView ? kMarkerGameKey : kMarkerSceneKey;
            frame.blackboard.Publish<int>(key, isGameView ? 1 : 2);
        });

    // Mirrors a "RenderOpaque"-like standing-in reader - ALSO
    // PerActiveView, registered as its own, separate provider (exactly
    // like PHASE7's own live harness uses
    // Core::RegisterProjectRenderPassProvider() for this role).
    pipeline.Register("RenderOpaque_Stub", ProviderScope::PerActiveView,
        [&fetchedByView](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const RenderPassId key = isGameView ? kMarkerGameKey : kMarkerSceneKey;
            const std::optional<int> fetched = frame.blackboard.Fetch<int>(key);
            // Store under a stable, test-local per-view integer key (1
            // for Game, 2 for Scene) - NOT RenderViewId's own opaque
            // hash, purely for readability of the assertions below.
            fetchedByView[isGameView ? 1u : 2u] = fetched.value_or(-1);
        });

    RenderPassBlackboard blackboard;
    blackboard.BeginFrame();

    RenderGraphBuilder builder;
    RenderPassFrameContext frame{
        /*activeViews=*/{ RenderViewId::Named("Game"), RenderViewId::Named("Scene") },
        /*currentView=*/RenderViewId::Shared(),
        /*blackboard=*/blackboard,
        /*builder=*/builder,
    };

    pipeline.DeclareInto(builder, frame);

    ASSERT_EQ(fetchedByView.size(), 2u);
    EXPECT_EQ(fetchedByView[1u], 1) << "Game's own reader must fetch Game's own published value (1), never Scene's.";
    EXPECT_EQ(fetchedByView[2u], 2) << "Scene's own reader must fetch Scene's own published value (2), never Game's.";
}

// The NEGATIVE control - proves this test fixture itself genuinely
// WOULD catch a cross-view aliasing bug, rather than trivially passing
// no matter what: a producer that Publish()es under ONE SHARED,
// view-agnostic key (the exact mistake Locked Design Decision #4 exists
// to prevent) causes the SECOND view processed to silently overwrite
// the FIRST view's result - both readers end up seeing the SAME
// (wrong, for one of them) value.
TEST(PreOpaqueRenderPipelineBlackboardQualificationTest,
    ASharedViewAgnosticKeyWouldCauseTheSecondViewToOverwriteTheFirst)
{
    constexpr RenderPassId kSharedKey = "Test.PreOpaqueMarker.SharedMistake"_passId;

    RenderPipeline pipeline;
    std::unordered_map<std::uint64_t, int> fetchedByView;

    pipeline.Register("PreOpaqueFeatures_BuggyStub", ProviderScope::PerActiveView,
        [](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            // BUG, DELIBERATE: ignores currentView - the exact mistake
            // Locked Design Decision #4 forbids.
            frame.blackboard.Publish<int>(kSharedKey, isGameView ? 1 : 2);
        });
    pipeline.Register("RenderOpaque_BuggyReaderStub", ProviderScope::PerActiveView,
        [&fetchedByView](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const std::optional<int> fetched = frame.blackboard.Fetch<int>(kSharedKey);
            fetchedByView[isGameView ? 1u : 2u] = fetched.value_or(-1);
        });

    RenderPassBlackboard blackboard;
    blackboard.BeginFrame();
    RenderGraphBuilder builder;
    RenderPassFrameContext frame{
        { RenderViewId::Named("Game"), RenderViewId::Named("Scene") },
        RenderViewId::Shared(), blackboard, builder,
    };

    pipeline.DeclareInto(builder, frame);

    // Scene runs SECOND (activeViews order above) and shares Game's own
    // key - "last-publish-wins" means Scene's publish (2) silently wins
    // for EVERYONE, including Game's own reader. This demonstrates the
    // real failure mode Rule 2/Locked Design Decision #4 prevents.
    ASSERT_EQ(fetchedByView.size(), 2u);
    EXPECT_EQ(fetchedByView[1u], 2) << "demonstrates the cross-view aliasing bug a shared key would cause";
    EXPECT_EQ(fetchedByView[2u], 2);
}

} // namespace gte::rg
