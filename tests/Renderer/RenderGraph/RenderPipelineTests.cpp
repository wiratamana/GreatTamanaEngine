// Unit tests for the render-pass-3 campaign's PHASE1
// (task_manager/render-pass-3/PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md) new
// declaration layer (src/Renderer/RenderGraph/RenderPipeline.h) -
// RenderPassId/RenderViewId hashing, RenderPassBlackboard's Publish()/
// Fetch()/BeginFrame() contract, and RenderPipeline::DeclareInto()'s
// Once/PerActiveView provider scoping + ordering + legacy-field-stamping
// behavior. Entirely Tier-1 - no live VkDevice needed, mirroring
// RenderGraphBuilderTests.cpp/RenderPassTests.cpp's own established style
// (a real RenderGraphBuilder instance with zero Vulkan device involved).

#include "Renderer/RenderGraph/RenderPipeline.h"

#include <gtest/gtest.h>

#include <vector>

namespace gte::rg {
namespace {

void NoOpExecute(PassContext&) { }
void NoOpSetup(RenderGraphBuilder::PassBuilder&) { }

// --- RenderPassId / RenderViewId hashing -----------------------------------

TEST(RenderPassIdTest, IdenticalLiteralsProduceEqualIds)
{
    constexpr RenderPassId a = "GpuSkinning"_passId;
    constexpr RenderPassId b = "GpuSkinning"_passId;
    EXPECT_TRUE(a == b);
}

TEST(RenderPassIdTest, DifferentLiteralsProduceDifferentIds)
{
    constexpr RenderPassId a = "GpuSkinning"_passId;
    constexpr RenderPassId b = "RenderOpaque"_passId;
    EXPECT_FALSE(a == b);
}

TEST(RenderViewIdTest, SharedIsAlwaysEqualToItself)
{
    EXPECT_TRUE(RenderViewId::Shared() == RenderViewId::Shared());
}

TEST(RenderViewIdTest, IdenticalNamedStringsProduceEqualViewIds)
{
    const RenderViewId a = RenderViewId::Named("GameView");
    const RenderViewId b = RenderViewId::Named("GameView");
    EXPECT_TRUE(a == b);
}

TEST(RenderViewIdTest, DifferentNamedStringsProduceDifferentViewIds)
{
    const RenderViewId gameView = RenderViewId::Named("GameView");
    const RenderViewId sceneView = RenderViewId::Named("SceneView");
    EXPECT_FALSE(gameView == sceneView);
}

TEST(RenderViewIdTest, NamedViewIsNeverEqualToShared)
{
    const RenderViewId named = RenderViewId::Named("GameView");
    EXPECT_FALSE(named == RenderViewId::Shared());
}

// --- RenderPassBlackboard --------------------------------------------------

TEST(RenderPassBlackboardTest, PublishThenFetchRoundTripsAValue)
{
    RenderPassBlackboard blackboard;
    constexpr RenderPassId key = "GpuSkinning.Output.Model42"_passId;

    blackboard.Publish<int>(key, 42);
    const std::optional<int> fetched = blackboard.Fetch<int>(key);

    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(*fetched, 42);
}

TEST(RenderPassBlackboardTest, FetchForNeverPublishedKeyReturnsNullopt)
{
    RenderPassBlackboard blackboard;
    constexpr RenderPassId key = "NeverPublished"_passId;

    const std::optional<int> fetched = blackboard.Fetch<int>(key);
    EXPECT_FALSE(fetched.has_value());
}

// std::any_cast's own pointer-overload type-mismatch behavior is what this
// relies on (returns nullptr rather than throwing) - confirmed explicitly
// here rather than assumed, per this phase's own doc (Step 3.5).
TEST(RenderPassBlackboardTest, FetchWithWrongTypeReturnsNullopt)
{
    RenderPassBlackboard blackboard;
    constexpr RenderPassId key = "Mismatched"_passId;

    blackboard.Publish<int>(key, 7);
    const std::optional<float> fetched = blackboard.Fetch<float>(key);

    EXPECT_FALSE(fetched.has_value());
}

TEST(RenderPassBlackboardTest, PublishOverwritesExistingSlotForSameKey)
{
    RenderPassBlackboard blackboard;
    constexpr RenderPassId key = "Overwritten"_passId;

    blackboard.Publish<int>(key, 1);
    blackboard.Publish<int>(key, 2);

    const std::optional<int> fetched = blackboard.Fetch<int>(key);
    ASSERT_TRUE(fetched.has_value());
    EXPECT_EQ(*fetched, 2);
}

TEST(RenderPassBlackboardTest, BeginFrameClearsAPreviouslyPublishedKey)
{
    RenderPassBlackboard blackboard;
    constexpr RenderPassId key = "ClearedAcrossFrames"_passId;

    blackboard.Publish<int>(key, 99);
    blackboard.BeginFrame();

    const std::optional<int> fetched = blackboard.Fetch<int>(key);
    EXPECT_FALSE(fetched.has_value());
}

// The "reused, not reallocated" contract (design doc Section 4/11) -
// BeginFrame() must never shrink the underlying storage's own capacity
// below its prior high-water mark, and a key published before BeginFrame()
// must no longer be fetchable afterwards.
TEST(RenderPassBlackboardTest, BeginFrameNeverShrinksCapacityBelowItsPriorHighWaterMark)
{
    RenderPassBlackboard blackboard;

    for (int i = 0; i < 8; ++i) {
        blackboard.Publish<int>(RenderPassId{ static_cast<std::uint64_t>(i + 1) }, i);
    }

    const std::size_t capacityBeforeBeginFrame = blackboard.SlotCapacityForTesting();
    blackboard.BeginFrame();
    const std::size_t capacityAfterBeginFrame = blackboard.SlotCapacityForTesting();

    EXPECT_GE(capacityAfterBeginFrame, capacityBeforeBeginFrame);

    // A stronger, behavioral proof that BeginFrame() really did clear the
    // logical contents (not just an incidental capacity observation).
    for (int i = 0; i < 8; ++i) {
        const RenderPassId key{ static_cast<std::uint64_t>(i + 1) };
        EXPECT_FALSE(blackboard.Fetch<int>(key).has_value());
    }
}

// --- RenderPipeline::DeclareInto() -----------------------------------------

TEST(RenderPipelineTest, OnceProviderContributesExactlyOnceRegardlessOfActiveViewCount)
{
    RenderPipeline pipeline;
    int callCount = 0;

    pipeline.Register("OnceProvider", ProviderScope::Once,
        [&](const RenderPassFrameContext&, std::vector<RenderPassDesc>& outPasses) {
            ++callCount;
            RenderPassDesc desc;
            desc.debugName = "OncePass";
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    RenderPassBlackboard blackboard;
    RenderGraphBuilder builder;
    RenderPassFrameContext frame{ {}, RenderViewId::Shared(), blackboard, {}, {} };
    frame.activeViews = { RenderViewId::Named("GameView"), RenderViewId::Named("SceneView") };

    pipeline.DeclareInto(builder, frame);

    EXPECT_EQ(callCount, 1);
    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "OncePass");
}

TEST(RenderPipelineTest, PerActiveViewProviderContributesOncePerActiveViewWithCorrectCurrentView)
{
    RenderPipeline pipeline;
    std::vector<RenderViewId> observedViews;

    pipeline.Register("PerViewProvider", ProviderScope::PerActiveView,
        [&](const RenderPassFrameContext& frame, std::vector<RenderPassDesc>& outPasses) {
            observedViews.push_back(frame.currentView);
            RenderPassDesc desc;
            desc.debugName = "PerViewPass";
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    RenderPassBlackboard blackboard;
    RenderGraphBuilder builder;
    const RenderViewId gameView = RenderViewId::Named("GameView");
    const RenderViewId sceneView = RenderViewId::Named("SceneView");
    RenderPassFrameContext frame{ { gameView, sceneView }, RenderViewId::Shared(), blackboard, {}, {} };

    pipeline.DeclareInto(builder, frame);

    ASSERT_EQ(observedViews.size(), 2u);
    EXPECT_TRUE(observedViews[0] == gameView);
    EXPECT_TRUE(observedViews[1] == sceneView);

    const CompiledGraphInput input = builder.Finish();
    EXPECT_EQ(input.passes.size(), 2u);
}

TEST(RenderPipelineTest, PerActiveViewProviderContributesNothingWithZeroActiveViews)
{
    RenderPipeline pipeline;
    int callCount = 0;

    pipeline.Register("PerViewProvider", ProviderScope::PerActiveView,
        [&](const RenderPassFrameContext&, std::vector<RenderPassDesc>& outPasses) {
            ++callCount;
            RenderPassDesc desc;
            desc.debugName = "PerViewPass";
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    RenderPassBlackboard blackboard;
    RenderGraphBuilder builder;
    RenderPassFrameContext frame{ {}, RenderViewId::Shared(), blackboard, {}, {} };

    pipeline.DeclareInto(builder, frame);

    EXPECT_EQ(callCount, 0);
    const CompiledGraphInput input = builder.Finish();
    EXPECT_TRUE(input.passes.empty());
}

TEST(RenderPipelineTest, CollectedPassesAreSortedByOrderRegardlessOfRegistrationOrProviderScope)
{
    RenderPipeline pipeline;

    // Registered deliberately out of order, and with a PerActiveView
    // provider's contribution interleaved between two Once providers, to
    // prove sorting is genuinely order/scope-independent.
    pipeline.Register("TransparentsProvider", ProviderScope::Once,
        [](const RenderPassFrameContext&, std::vector<RenderPassDesc>& outPasses) {
            RenderPassDesc desc;
            desc.debugName = "TransparentsPass";
            desc.order = RenderPassEvent::Transparents;
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    pipeline.Register("PerViewOpaqueProvider", ProviderScope::PerActiveView,
        [](const RenderPassFrameContext&, std::vector<RenderPassDesc>& outPasses) {
            RenderPassDesc desc;
            desc.debugName = "OpaquePass";
            desc.order = RenderPassEvent::Opaques;
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    pipeline.Register("PreOpaquesProvider", ProviderScope::Once,
        [](const RenderPassFrameContext&, std::vector<RenderPassDesc>& outPasses) {
            RenderPassDesc desc;
            desc.debugName = "PreOpaquesPass";
            desc.order = RenderPassEvent::PreOpaques;
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    RenderPassBlackboard blackboard;
    RenderGraphBuilder builder;
    RenderPassFrameContext frame{ { RenderViewId::Named("GameView") }, RenderViewId::Shared(), blackboard, {}, {} };

    pipeline.DeclareInto(builder, frame);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 3u);
    EXPECT_STREQ(input.passes[0].name, "PreOpaquesPass");
    EXPECT_STREQ(input.passes[1].name, "OpaquePass");
    EXPECT_STREQ(input.passes[2].name, "TransparentsPass");
}

// The ONE test in this file that directly protects PHASE0's Locked Design
// Decision 5 - the old ViewScope/RenderPassCategory/RenderPassDrawKind
// fields must never silently stop being stamped once a pass is declared
// through this NEW layer instead of the old, direct AddRenderPass() call.
TEST(RenderPipelineTest, LegacyCategoryAndDrawKindSurviveUnchangedIntoTheProducedPassRecord)
{
    RenderPipeline pipeline;

    pipeline.Register("SkyProvider", ProviderScope::Once,
        [](const RenderPassFrameContext&, std::vector<RenderPassDesc>& outPasses) {
            RenderPassDesc desc;
            desc.debugName = "DrawSkyBackground";
            desc.kind = PassKind::Graphics;
            desc.legacyCategory = RenderPassCategory::AtmosphereLut;
            desc.drawKind = RenderPassDrawKind::DrawQuad;
            desc.order = RenderPassEvent::AfterOpaques;
            desc.setup = NoOpSetup;
            desc.execute = NoOpExecute;
            outPasses.push_back(desc);
        });

    RenderPassBlackboard blackboard;
    RenderGraphBuilder builder;
    RenderPassFrameContext frame{ {}, RenderViewId::Shared(), blackboard, {}, {} };

    pipeline.DeclareInto(builder, frame);

    const CompiledGraphInput input = builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    EXPECT_STREQ(input.passes[0].name, "DrawSkyBackground");
    EXPECT_EQ(input.passes[0].kind, PassKind::Graphics);
    EXPECT_EQ(input.passes[0].category, RenderPassCategory::AtmosphereLut);
    EXPECT_EQ(input.passes[0].drawKind, RenderPassDrawKind::DrawQuad);
    EXPECT_EQ(input.passes[0].renderPassEvent, RenderPassEvent::AfterOpaques);
}

TEST(RenderPipelineTest, UnregisterRemovesAMatchingProviderByDebugNameContent)
{
    RenderPipeline pipeline;
    int callCount = 0;

    // A separate, non-string-literal-folded buffer, so the two `const
    // char*` values genuinely differ by POINTER even though their CONTENT
    // is identical - proving Unregister()'s own strcmp() fallback, not just
    // pointer equality.
    char debugNameBuffer[] = "RemovableProvider";

    pipeline.Register(debugNameBuffer, ProviderScope::Once,
        [&](const RenderPassFrameContext&, std::vector<RenderPassDesc>&) { ++callCount; });

    pipeline.Unregister("RemovableProvider");

    RenderPassBlackboard blackboard;
    RenderGraphBuilder builder;
    RenderPassFrameContext frame{ {}, RenderViewId::Shared(), blackboard, {}, {} };
    pipeline.DeclareInto(builder, frame);

    EXPECT_EQ(callCount, 0);
}

} // namespace
} // namespace gte::rg
