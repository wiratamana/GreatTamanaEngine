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

#include <array>
#include <cstdint>
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
        context.view = rg::RenderViewId::Named("Primary");
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

Entity MakeEntity(std::uint32_t index)
{
    Entity entity;
    entity.index = index;
    entity.generation = 1;
    return entity;
}

TEST(KitSceneTest, SelectEntitiesFillsOneSetPerCallAndRefusesAnEmptySpanOrAnExhaustedPool)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Scene scene(*core);
    PreOpaqueFrame frame = scene.MakeFrame();

    const std::array<Entity, 2> entities{ MakeEntity(3), MakeEntity(5) };
    EXPECT_FALSE(frame.SelectEntities({}).IsValid());

    const EntitySelection first = frame.SelectEntities(entities);
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(first.entities.size(), entities.size());
    const EntityMarkSet& set = *static_cast<const EntityMarkSet*>(first.handle);
    EXPECT_TRUE(set.Contains(MakeEntity(3)));
    EXPECT_TRUE(set.Contains(MakeEntity(5)));
    EXPECT_FALSE(set.Contains(MakeEntity(4)));

    for (std::size_t i = 1; i < Core::kMaxExclusionSetsPerFrame; ++i) {
        EXPECT_TRUE(frame.SelectEntities(entities).IsValid());
    }
    EXPECT_FALSE(frame.SelectEntities(entities).IsValid());
    EXPECT_EQ(CountByCategory(sink, "FeatureKit"), 1); // One error for the exhausted pool.
}

TEST(KitSceneTest, ClaimNeedsASelectionAndAShadedSetNeedsSceneServices)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Scene scene(*core);
    PreOpaqueFrame frame = scene.MakeFrame();

    OffscreenTarget target("Test.Map");
    const Texture map = target.Acquire(frame, DepthOnly{ 32 });
    ASSERT_TRUE(map.IsValid());

    SceneShaderSet depth = SceneShaderSet::DepthOnly("DoesNotExistDepth");
    EXPECT_FALSE(frame.SceneDraw("Test.Claim").Shaders(depth).ClearDepthTarget(map, 1.0f).Claim().Record());

    SceneShaderSet shaded = SceneShaderSet::Shaded("DoesNotExistShaded");
    EXPECT_FALSE(frame.SceneDraw("Test.Shaded").Shaders(shaded).ClearDepthTarget(map, 1.0f).Record());

    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountByCategory(sink, "FeatureKit"), 2);
    EXPECT_EQ(CountByCategory(sink, "SceneShaderSet"), 0); // Refused before any build was tried.
}

TEST(KitSceneTest, AnInvalidSelectionAndAClaimInASharedFrameDeclareNothingAndStaySilent)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Scene scene(*core);
    scene.context.scope = rg::ViewScope::Shared;
    PreOpaqueFrame frame = scene.MakeFrame();

    OffscreenTarget target("Test.Map");
    const Texture map = target.Acquire(frame, DepthOnly{ 32 });
    ASSERT_TRUE(map.IsValid());
    SceneShaderSet depth = SceneShaderSet::DepthOnly("DoesNotExistDepth");

    EXPECT_FALSE(frame.SceneDraw("Test.NoSelection").Shaders(depth).ClearDepthTarget(map, 1.0f).Only(EntitySelection{}).Record());

    const std::array<Entity, 1> entities{ MakeEntity(1) };
    const EntitySelection selection = frame.SelectEntities(entities);
    ASSERT_TRUE(selection.IsValid());
    EXPECT_FALSE(frame.SceneDraw("Test.SharedClaim")
                     .Shaders(depth)
                     .ClearDepthTarget(map, 1.0f)
                     .Only(selection)
                     .Claim()
                     .Record());

    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountByCategory(sink, "FeatureKit"), 0);
    EXPECT_EQ(CountByCategory(sink, "SceneShaderSet"), 0);
}

} // namespace
} // namespace gte::fx
