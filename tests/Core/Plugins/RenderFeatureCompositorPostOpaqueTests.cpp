// Tier-1 tests for RenderFeatureCompositor::RegisterPostOpaqueFeature()/
// UnregisterPostOpaqueFeature().
//
// Mirrors RenderFeatureCompositorPreOpaqueTests.cpp's own exact
// HeadlessSurfaceProvider + NoopHostServices + real Core fixture pattern,
// GTEST_SKIP()-ing identically if this machine's Vulkan driver/loader
// doesn't support VK_EXT_headless_surface.

#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace gte {
namespace {

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override { }
};

std::unique_ptr<Core> TryMakeHeadlessCore(HeadlessSurfaceProvider& surfaceProvider, NoopHostServices& hostServices)
{
    try {
        return std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception&) {
        return nullptr;
    }
}

const RenderFeatureDebugEntry* FindByName(const std::vector<RenderFeatureDebugEntry>& snapshot, const std::string& name)
{
    for (const RenderFeatureDebugEntry& entry : snapshot) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

#define GTE_SKIP_IF_NO_HEADLESS_CORE(core)                                                                           \
    HeadlessSurfaceProvider surfaceProvider;                                                                         \
    NoopHostServices hostServices;                                                                                   \
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);                                 \
    if (core == nullptr) {                                                                                           \
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "                \
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "                    \
                        "HeadlessSurfaceProvider.h's own top-of-file comment).";                                     \
    }

TEST(RenderFeatureCompositorPostOpaqueTest, RegisterPostOpaqueFeatureSucceedsForAFreshUniqueName)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const bool registered = compositor->RegisterPostOpaqueFeature("POp_Test_FreshUniqueName", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    EXPECT_TRUE(registered);

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* found = FindByName(snapshot, "POp_Test_FreshUniqueName");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->stage, "PostOpaque");
    EXPECT_EQ(found->blendMode, "None");
    EXPECT_TRUE(found->isProjectFeature);
}

TEST(RenderFeatureCompositorPostOpaqueTest, RegisteringADuplicateNameFails)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_DuplicateOriginal", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_FALSE(compositor->RegisterPostOpaqueFeature("POp_Test_DuplicateOriginal", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(RenderFeatureCompositorPostOpaqueTest, NameAlreadyUsedByAPostCompositeEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor postCompositeDescriptor = MakeRenderFeatureDescriptor(
        "POp_Test_CrossNamespaceCollision", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(compositor->RegisterProjectFeature(
        postCompositeDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    EXPECT_FALSE(compositor->RegisterPostOpaqueFeature("POp_Test_CrossNamespaceCollision", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

// The symmetric reverse of the test immediately above.
TEST(RenderFeatureCompositorPostOpaqueTest, RegisterProjectFeatureRefusesANameAlreadyUsedByAPostOpaqueEntry)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_ReverseCrossNamespaceCollision", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    const GtePluginRenderFeatureDescriptor postCompositeDescriptor = MakeRenderFeatureDescriptor(
        "POp_Test_ReverseCrossNamespaceCollision", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    EXPECT_FALSE(compositor->RegisterProjectFeature(
        postCompositeDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));
}

TEST(RenderFeatureCompositorPostOpaqueTest, UnregisterOnAnUnknownNameFailsHarmlessly)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    EXPECT_FALSE(compositor->UnregisterPostOpaqueFeature("POp_Test_ThisNameWasNeverRegistered"));
}

TEST(RenderFeatureCompositorPostOpaqueTest, RegisterUnregisterReRegisterRoundTripSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_RoundTrip", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_TRUE(compositor->UnregisterPostOpaqueFeature("POp_Test_RoundTrip"));
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), "POp_Test_RoundTrip"), nullptr);
    EXPECT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_RoundTrip", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), "POp_Test_RoundTrip"), nullptr);
}

TEST(RenderFeatureCompositorPostOpaqueTest, SetFeatureEnabledAndSetFeaturePriorityApplyToAPostOpaqueEntry)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_EnabledAndPriority", 5,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    EXPECT_TRUE(compositor->SetFeatureEnabled("POp_Test_EnabledAndPriority", false));
    const RenderFeatureDebugEntry* foundAfterDisable =
        FindByName(compositor->DebugSnapshot(), "POp_Test_EnabledAndPriority");
    ASSERT_NE(foundAfterDisable, nullptr);
    EXPECT_FALSE(foundAfterDisable->enabled);

    EXPECT_TRUE(compositor->SetFeaturePriority("POp_Test_EnabledAndPriority", 42));
    const RenderFeatureDebugEntry* foundAfterPriority =
        FindByName(compositor->DebugSnapshot(), "POp_Test_EnabledAndPriority");
    ASSERT_NE(foundAfterPriority, nullptr);
    EXPECT_EQ(foundAfterPriority->priority, 42);
}

// A same-priority collision between two PostOpaque entries falls back to the
// documented, stable lexical tie-break (SortAndDetectCollisionsInPostOpaqueList()),
// never crashing.
TEST(RenderFeatureCompositorPostOpaqueTest, SamePriorityCollisionFallsBackToLexicalTieBreakWithoutCrashing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_CollisionB", 7,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_CollisionA", 7,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_NE(FindByName(snapshot, "POp_Test_CollisionA"), nullptr);
    EXPECT_NE(FindByName(snapshot, "POp_Test_CollisionB"), nullptr);
}

// Priority ordering - PostOpaqueFeaturesInPriorityOrder() must return entries
// ascending by priority regardless of registration order.
TEST(RenderFeatureCompositorPostOpaqueTest, PostOpaqueFeaturesInPriorityOrderReturnsAscendingByPriority)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_OrderC", 30,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_OrderA", 10,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_OrderB", 20,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    const std::vector<RenderFeatureCompositor::PostOpaqueEntry>& ordered =
        compositor->PostOpaqueFeaturesInPriorityOrder();
    ASSERT_GE(ordered.size(), 3u);

    int lastPriority = -1000000;
    for (const RenderFeatureCompositor::PostOpaqueEntry& entry : ordered) {
        if (entry.name == "POp_Test_OrderA" || entry.name == "POp_Test_OrderB" || entry.name == "POp_Test_OrderC") {
            EXPECT_GE(entry.priority, lastPriority);
            lastPriority = entry.priority;
        }
    }
}

// Debug fix D1 (RENDER_GRAPH_BUG_REPORT.txt) - a built-in engine PostOpaque
// feature (RegisterBuiltInPostOpaqueFeature(), the real-world shape of
// ShadowFeature.cpp's "Shadow.Mask") must report isProjectFeature == false
// in DebugSnapshot() - a Project-owned PostOpaque feature registered right
// alongside it must still report true. Mirrors
// RenderFeatureCompositorPreOpaqueTests.cpp's own identical test for the
// PreOpaque stage.
TEST(RenderFeatureCompositorPostOpaqueTest, BuiltInFeatureReportsIsProjectFeatureFalse)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterBuiltInPostOpaqueFeature("POp_Test_EngineProbe", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    ASSERT_TRUE(compositor->RegisterPostOpaqueFeature("POp_Test_ProjectProbe", 1,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* engineEntry = FindByName(snapshot, "POp_Test_EngineProbe");
    const RenderFeatureDebugEntry* projectEntry = FindByName(snapshot, "POp_Test_ProjectProbe");
    ASSERT_NE(engineEntry, nullptr);
    ASSERT_NE(projectEntry, nullptr);
    EXPECT_FALSE(engineEntry->isProjectFeature);
    EXPECT_TRUE(projectEntry->isProjectFeature);
}

} // namespace gte
