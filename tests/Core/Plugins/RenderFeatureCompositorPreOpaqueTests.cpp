// Tier-1 tests for RenderFeatureCompositor::RegisterPreOpaqueFeature()/
// UnregisterPreOpaqueFeature() (better-render-pass-5 effort, BLOCK 3,
// PHASE2/PHASE6 -
// task_manager/better-render-pass-5/PHASE6_TIER1_TEST_COVERAGE.md).
//
// Mirrors RenderFeatureCompositorProjectFeatureTests.cpp's own exact
// HeadlessSurfaceProvider + NoopHostServices + real Core fixture
// pattern, GTEST_SKIP()-ing identically if this machine's Vulkan
// driver/loader doesn't support VK_EXT_headless_surface.

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

TEST(RenderFeatureCompositorPreOpaqueTest, RegisterPreOpaqueFeatureSucceedsForAFreshUniqueName)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const bool registered = compositor->RegisterPreOpaqueFeature(
        "PO_Test_FreshUniqueName", 0, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { });
    EXPECT_TRUE(registered);

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* found = FindByName(snapshot, "PO_Test_FreshUniqueName");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->stage, "PreOpaque");
    EXPECT_EQ(found->blendMode, "None");
    EXPECT_TRUE(found->isProjectFeature);
}

TEST(RenderFeatureCompositorPreOpaqueTest, RegisteringADuplicateNameFails)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_DuplicateOriginal", 0, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    EXPECT_FALSE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_DuplicateOriginal", 0, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
}

// PHASE0 Locked Design Decision #8 - PreOpaque names share a GLOBAL
// namespace with PostComposite/PreUI.
TEST(RenderFeatureCompositorPreOpaqueTest, NameAlreadyUsedByAPostCompositeEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor postCompositeDescriptor = MakeRenderFeatureDescriptor(
        "PO_Test_CrossNamespaceCollision", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(compositor->RegisterProjectFeature(
        postCompositeDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    EXPECT_FALSE(compositor->RegisterPreOpaqueFeature("PO_Test_CrossNamespaceCollision", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
}

// The symmetric reverse of the test immediately above.
TEST(RenderFeatureCompositorPreOpaqueTest, RegisterProjectFeatureRefusesANameAlreadyUsedByAPreOpaqueEntry)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPreOpaqueFeature("PO_Test_ReverseCrossNamespaceCollision", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    const GtePluginRenderFeatureDescriptor postCompositeDescriptor = MakeRenderFeatureDescriptor(
        "PO_Test_ReverseCrossNamespaceCollision", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    EXPECT_FALSE(compositor->RegisterProjectFeature(
        postCompositeDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));
}

TEST(RenderFeatureCompositorPreOpaqueTest, UnregisterOnAnUnknownNameFailsHarmlessly)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    EXPECT_FALSE(compositor->UnregisterPreOpaqueFeature("PO_Test_ThisNameWasNeverRegistered"));
}

TEST(RenderFeatureCompositorPreOpaqueTest, RegisterUnregisterReRegisterRoundTripSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_RoundTrip", 0, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    EXPECT_TRUE(compositor->UnregisterPreOpaqueFeature("PO_Test_RoundTrip"));
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), "PO_Test_RoundTrip"), nullptr);
    EXPECT_TRUE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_RoundTrip", 0, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), "PO_Test_RoundTrip"), nullptr);
}

// Confirms SetFeatureEnabled()/SetFeaturePriority() (RenderFeatureCompositor,
// PHASE2's own extension) correctly reach a PreOpaque entry via the new
// FindPreOpaqueEntryByName() fallback path.
TEST(RenderFeatureCompositorPreOpaqueTest, SetFeatureEnabledAndSetFeaturePriorityApplyToAPreOpaqueEntry)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_EnabledAndPriority", 5, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    EXPECT_TRUE(compositor->SetFeatureEnabled("PO_Test_EnabledAndPriority", false));
    const RenderFeatureDebugEntry* foundAfterDisable =
        FindByName(compositor->DebugSnapshot(), "PO_Test_EnabledAndPriority");
    ASSERT_NE(foundAfterDisable, nullptr);
    EXPECT_FALSE(foundAfterDisable->enabled);

    EXPECT_TRUE(compositor->SetFeaturePriority("PO_Test_EnabledAndPriority", 42));
    const RenderFeatureDebugEntry* foundAfterPriority =
        FindByName(compositor->DebugSnapshot(), "PO_Test_EnabledAndPriority");
    ASSERT_NE(foundAfterPriority, nullptr);
    EXPECT_EQ(foundAfterPriority->priority, 42);
}

// A same-priority collision between two PreOpaque entries falls back to
// the documented, stable lexical tie-break (SortAndDetectCollisionsInPreOpaqueList()),
// never crashing - mirrors this engine's own established collision
// policy for every other stage's priority sort.
TEST(RenderFeatureCompositorPreOpaqueTest, SamePriorityCollisionFallsBackToLexicalTieBreakWithoutCrashing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_CollisionB", 7, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    ASSERT_TRUE(compositor->RegisterPreOpaqueFeature(
        "PO_Test_CollisionA", 7, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_NE(FindByName(snapshot, "PO_Test_CollisionA"), nullptr);
    EXPECT_NE(FindByName(snapshot, "PO_Test_CollisionB"), nullptr);
}

} // namespace gte
