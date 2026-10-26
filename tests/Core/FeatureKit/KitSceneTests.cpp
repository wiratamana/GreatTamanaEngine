// Tier-2 (headless GPU) tests for the scene verbs: offscreen targets, scene shader sets and scene draws.
// They skip when this machine cannot make a headless Core.

#include "Core/Core.h"
#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "Core/IHostServices.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/RenderGraph.h"

#include "../../Fakes/HeadlessSurfaceProvider.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace gte::fx {
namespace {

using test::CountByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel, std::string_view) override { }
};

std::unique_ptr<Core> TryMakeHeadlessCore(HeadlessSurfaceProvider& surface, NoopHostServices& host)
{
    try {
        return std::make_unique<Core>(surface, host);
    } catch (const std::exception&) {
        return nullptr;
    }
}

// A frame over a real Core and a bare builder, the way the module adapter makes one.
struct Scene {
    explicit Scene(Core& core)
    {
        this->core = &core;
        builder.SetRenderer(&core.GetRenderer());
        builder.SetPersistentResourceCache(nullptr, rg::ExecuteTimingMode::SynchronousImmediateReadback, 1);
        context.builder = &builder;
        context.blackboard = &blackboard;
        context.core = &core;
        context.view = rg::RenderViewId::Named("Game");
        context.featureName = "Feature";
        context.event = rg::RenderPassEvent::PreOpaques;
        context.scope = rg::ViewScope::Primary;
        context.serial = 1;
    }

    PreOpaqueFrame MakeFrame() { return PreOpaqueFrame(View{}, core->GetRegistry(), 1, &context); }

    Core* core = nullptr;
    rg::RenderGraphBuilder builder;
    rg::RenderPassBlackboard blackboard;
    detail::FrameContext context;
};

#define GTE_SKIP_IF_NO_HEADLESS_CORE(core)                                                                           \
    HeadlessSurfaceProvider surface;                                                                                 \
    NoopHostServices host;                                                                                           \
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surface, host);                                                 \
    if (core == nullptr) {                                                                                           \
        GTEST_SKIP() << "No headless Core on this machine (VK_EXT_headless_surface missing).";                       \
    }

TEST(KitSceneTest, OffscreenTargetImportsAKeptDepthTextureAndKeepsItsFirstSize)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Scene scene(*core);

    PreOpaqueFrame frame = scene.MakeFrame();

    OffscreenTarget target("Test.Map");
    const Texture first = target.Acquire(frame, DepthOnly{ 64 });
    ASSERT_TRUE(first.IsValid());
    const std::optional<rg::TextureDesc> desc = scene.builder.DescOf(first.handle);
    ASSERT_TRUE(desc.has_value());
    EXPECT_FALSE(desc->hasColor);
    EXPECT_TRUE(desc->hasDepth);
    EXPECT_EQ(desc->width, 64u);

    const Texture second = target.Acquire(frame, DepthOnly{ 128 });
    const Texture third = target.Acquire(frame, DepthOnly{ 128 });
    EXPECT_TRUE(second.IsValid());
    EXPECT_TRUE(third.IsValid());
    EXPECT_EQ(scene.builder.DescOf(second.handle)->width, 64u);
    EXPECT_EQ(CountByCategory(sink, "FeatureKit"), 1);
}

TEST(KitSceneTest, ASceneDrawWithAShaderSetThatCannotBuildDeclaresNothing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Scene scene(*core);

    PreOpaqueFrame frame = scene.MakeFrame();

    OffscreenTarget target("Test.Map");
    const Texture map = target.Acquire(frame, DepthOnly{ 32 });
    SceneShaderSet shaders = SceneShaderSet::DepthOnly("DoesNotExistDepth");

    EXPECT_FALSE(frame.SceneDraw("Test.Draw").Shaders(shaders).Camera(Mat4::Identity()).ClearDepthTarget(map, 1.0f).Record());
    EXPECT_FALSE(shaders.Ready());
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountByCategory(sink, "SceneShaderSet"), 1); // One warning for the failed build.
}

TEST(KitSceneTest, ADisabledSceneDrawPassDeclaresNothingAndLogsNothing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Scene scene(*core);

    rg::RenderPassToggleRegistry toggles;
    toggles.SetEnabled("Test.Draw", false);
    scene.context.toggles = &toggles;
    PreOpaqueFrame frame = scene.MakeFrame();

    SceneShaderSet shaders = SceneShaderSet::DepthOnly("DoesNotExistDepth");
    EXPECT_FALSE(frame.SceneDraw("Test.Draw").Shaders(shaders).Record());
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountByCategory(sink, "SceneShaderSet"), 0);
}

} // namespace
} // namespace gte::fx
