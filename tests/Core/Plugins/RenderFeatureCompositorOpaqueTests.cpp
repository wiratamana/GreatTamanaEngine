// Tier-1 tests for the Opaque stage list of RenderFeatureCompositor.
// Needs a headless Core; skips when the machine has no headless surface support.

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

bool RegisterOpaque(RenderFeatureCompositor& compositor, const std::string& name, std::int32_t priority)
{
    return compositor.RegisterStageFeature(CompositorStage::Opaque, name, priority,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
}

} // namespace

#define GTE_SKIP_IF_NO_HEADLESS_CORE(core)                                                                           \
    HeadlessSurfaceProvider surfaceProvider;                                                                         \
    NoopHostServices hostServices;                                                                                   \
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);                                 \
    if (core == nullptr) {                                                                                           \
        GTEST_SKIP() << "Core construction needs a headless Vulkan surface.";                                        \
    }

TEST(RenderFeatureCompositorOpaqueTest, RegisterReportsTheOpaqueStageInTheSnapshot)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(RegisterOpaque(*compositor, "Opq_Test_Fresh", 0));
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* found = FindByName(snapshot, "Opq_Test_Fresh");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->stage, "Opaque");
    EXPECT_TRUE(found->enabled);
}

TEST(RenderFeatureCompositorOpaqueTest, UnregisterRemovesTheEntryAndAllowsReRegistration)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(RegisterOpaque(*compositor, "Opq_Test_RoundTrip", 0));
    EXPECT_TRUE(compositor->UnregisterStageFeature(CompositorStage::Opaque, "Opq_Test_RoundTrip"));
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), "Opq_Test_RoundTrip"), nullptr);
    EXPECT_FALSE(compositor->UnregisterStageFeature(CompositorStage::Opaque, "Opq_Test_RoundTrip"));
    EXPECT_TRUE(RegisterOpaque(*compositor, "Opq_Test_RoundTrip", 0));
}

TEST(RenderFeatureCompositorOpaqueTest, EnabledOverrideAndPriorityApplyToAnOpaqueEntry)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(RegisterOpaque(*compositor, "Opq_Test_Late", 50));
    ASSERT_TRUE(RegisterOpaque(*compositor, "Opq_Test_Early", 10));

    EXPECT_TRUE(compositor->SetFeatureEnabled("Opq_Test_Late", false));
    const RenderFeatureDebugEntry* disabled = FindByName(compositor->DebugSnapshot(), "Opq_Test_Late");
    ASSERT_NE(disabled, nullptr);
    EXPECT_FALSE(disabled->enabled);

    EXPECT_TRUE(compositor->SetFeaturePriority("Opq_Test_Late", 1));
    const std::vector<SceneStageEntry>& ordered = compositor->StageFeaturesInPriorityOrder<CompositorStage::Opaque>();
    ASSERT_GE(ordered.size(), 2u);
    EXPECT_EQ(ordered.front().name, "Opq_Test_Late");
}

TEST(RenderFeatureCompositorOpaqueTest, NamesAreSharedWithEveryOtherStageInBothDirections)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    ASSERT_TRUE(compositor->RegisterStageFeature(CompositorStage::PostOpaque, "Opq_Test_SharedA", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_FALSE(RegisterOpaque(*compositor, "Opq_Test_SharedA", 0));

    ASSERT_TRUE(RegisterOpaque(*compositor, "Opq_Test_SharedB", 0));
    EXPECT_FALSE(compositor->RegisterStageFeature(CompositorStage::PreOpaque, "Opq_Test_SharedB", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    EXPECT_FALSE(compositor->RegisterStageFeature(CompositorStage::PostTransparent, "Opq_Test_SharedB", 0,
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_FALSE(compositor->RegisterProjectFeature(
        MakeRenderFeatureDescriptor("Opq_Test_SharedB", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace),
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, Extent2D,
            const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));
}

} // namespace gte
