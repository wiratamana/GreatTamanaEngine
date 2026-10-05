// Tier-1 test proving ALL FIVE RenderFeatureCompositor registration stages -
// PostComposite, PreUI, PreOpaque, PostOpaque, PostTransparent - genuinely
// share ONE global feature-name namespace, in EVERY ordered direction (20
// ordered pairs total). Mirrors RenderFeatureCompositorPreOpaqueTests.cpp's
// own HeadlessSurfaceProvider + NoopHostServices + real Core fixture
// pattern, GTEST_SKIP()-ing identically if this machine's Vulkan
// driver/loader doesn't support VK_EXT_headless_surface.

#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <array>
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

// Registers `name` under exactly one of the five stages, by label, using
// whichever real public entry point that stage actually uses. Returns
// whatever that entry point itself returns.
bool RegisterUnderStage(RenderFeatureCompositor& compositor, const std::string& stageLabel, const std::string& name)
{
    if (stageLabel == "PostComposite") {
        return compositor.RegisterProjectFeature(
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace),
            [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { });
    }
    if (stageLabel == "PreUI") {
        return compositor.RegisterProjectFeature(
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PreUI, 0, RenderFeatureBlendMode::Replace),
            [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { });
    }
    if (stageLabel == "PreOpaque") {
        return compositor.RegisterPreOpaqueFeature(
            name, 0, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { });
    }
    if (stageLabel == "PostOpaque") {
        return compositor.RegisterPostOpaqueFeature(name, 0,
            [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    }
    if (stageLabel == "PostTransparent") {
        return compositor.RegisterPostTransparentFeature(name, 0,
            [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    }
    return false;
}

} // namespace

TEST(RenderFeatureCompositorFiveStageNamespaceTest, EveryOrderedPairOfStagesRefusesASharedName)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);
    if (core == nullptr) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment).";
    }

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    static constexpr std::array<const char*, 5> kStages = { "PostComposite", "PreUI", "PreOpaque", "PostOpaque",
        "PostTransparent" };

    for (const char* firstStage : kStages) {
        for (const char* secondStage : kStages) {
            if (firstStage == secondStage) {
                continue;
            }

            const std::string name =
                std::string("FiveStageNamespace_") + firstStage + "_Then_" + secondStage;

            ASSERT_TRUE(RegisterUnderStage(*compositor, firstStage, name))
                << "First registration under " << firstStage << " for name '" << name << "' was expected to succeed.";
            EXPECT_FALSE(RegisterUnderStage(*compositor, secondStage, name))
                << "Registering '" << name << "' under " << secondStage << " after it was already claimed by "
                << firstStage << " should have been refused.";
        }
    }
}

} // namespace gte
