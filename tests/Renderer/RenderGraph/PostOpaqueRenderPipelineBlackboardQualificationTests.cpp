// Tier-1 tests for the per-view Blackboard qualification guarantee, for
// BOTH the PostOpaque and PostTransparent stages (combined into one file
// per their shared shape - mirroring one another closely enough that
// splitting them would be pure duplication).
//
// Mirrors PreOpaqueRenderPipelineBlackboardQualificationTests.cpp's own
// established fixture pattern exactly - pure rg::RenderPipeline/
// RenderGraphBuilder/RenderPassBlackboard usage, WITHOUT needing a live
// Vulkan device, Core, or RenderFeatureCompositor. Shape: a
// "PostOpaqueFeatures"/"PostTransparentFeatures"-like producer provider
// writes a distinct marker value (an int tag, 1 for Game / 2 for Scene)
// into the Blackboard under a key qualified by frame.currentView, mirroring
// Core.cpp's real production providers exactly (same ProviderScope::
// PerActiveView, same per-view key derivation idiom). A standing-in reader
// provider, registered separately, Fetches that SAME per-view key and
// records what it actually got into a small, test-local, per-view result
// map. After DeclareInto() completes for BOTH views, this asserts the
// reader saw EXACTLY the tag matching ITS OWN view, for BOTH views -
// proving no cross-view aliasing happened.

#include "Renderer/RenderGraph/RenderPipeline.h"

#include <gtest/gtest.h>

#include <unordered_map>

namespace gte::rg {
namespace {

constexpr RenderPassId kPostOpaqueMarkerGameKey = "Test.PostOpaqueMarker.Game"_passId;
constexpr RenderPassId kPostOpaqueMarkerSceneKey = "Test.PostOpaqueMarker.Scene"_passId;
constexpr RenderPassId kPostTransparentMarkerGameKey = "Test.PostTransparentMarker.Game"_passId;
constexpr RenderPassId kPostTransparentMarkerSceneKey = "Test.PostTransparentMarker.Scene"_passId;

} // namespace

TEST(PostOpaqueRenderPipelineBlackboardQualificationTest,
    EachViewsReaderFetchesExactlyItsOwnViewsPublishedValueNeverTheOtherViews)
{
    RenderPipeline pipeline;
    std::unordered_map<std::uint64_t, int> fetchedByView;

    pipeline.Register("PostOpaqueFeatures_Stub", ProviderScope::PerActiveView,
        [](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const RenderPassId key = isGameView ? kPostOpaqueMarkerGameKey : kPostOpaqueMarkerSceneKey;
            frame.blackboard.Publish<int>(key, isGameView ? 1 : 2);
        });

    pipeline.Register("PostComposite_Stub", ProviderScope::PerActiveView,
        [&fetchedByView](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const RenderPassId key = isGameView ? kPostOpaqueMarkerGameKey : kPostOpaqueMarkerSceneKey;
            const std::optional<int> fetched = frame.blackboard.Fetch<int>(key);
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

TEST(PostTransparentRenderPipelineBlackboardQualificationTest,
    EachViewsReaderFetchesExactlyItsOwnViewsPublishedValueNeverTheOtherViews)
{
    RenderPipeline pipeline;
    std::unordered_map<std::uint64_t, int> fetchedByView;

    pipeline.Register("PostTransparentFeatures_Stub", ProviderScope::PerActiveView,
        [](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const RenderPassId key = isGameView ? kPostTransparentMarkerGameKey : kPostTransparentMarkerSceneKey;
            frame.blackboard.Publish<int>(key, isGameView ? 1 : 2);
        });

    pipeline.Register("PostComposite_Stub", ProviderScope::PerActiveView,
        [&fetchedByView](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>&) {
            const bool isGameView = (frame.currentView == RenderViewId::Named("Game"));
            const RenderPassId key = isGameView ? kPostTransparentMarkerGameKey : kPostTransparentMarkerSceneKey;
            const std::optional<int> fetched = frame.blackboard.Fetch<int>(key);
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

} // namespace gte::rg
