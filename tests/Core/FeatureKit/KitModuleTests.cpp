// Module adapter tests. The per-callback logic runs on a bare builder (no GPU). The headless-Core tests
// check the real registration path and skip when this machine cannot make a Core.

#include "Core/Core.h"
#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/RegistrationRunner.h"
#include "Core/IHostServices.h"
#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include "../../Fakes/HeadlessSurfaceProvider.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace gte::fx {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

View GameView()
{
    View view;
    view.scope = rg::ViewScope::Primary;
    view.extent = Extent2D{ 64, 32 };
    view.eye = Vec3(1.0f, 2.0f, 3.0f);
    return view;
}

// Everything RunRegistration needs, on a bare builder.
struct Runner {
    Runner()
    {
        context.builder = &builder;
        context.featureName = "KitTest";
        context.toggles = &toggles;
    }

    bool Run(const Registration& registration, const std::optional<View>& view, std::uint64_t frame,
        SharedOncePerFrame* latch = nullptr)
    {
        detail::RunInput input;
        input.registration = &registration;
        input.sharedLatch = latch;
        input.view = &view;
        input.scene = &scene;
        input.context = &context;
        input.frameIndex = frame;
        input.serial = FrameSerial(frame, view ? view->scope : rg::ViewScope::Shared);
        return detail::RunRegistration(input, failures);
    }

    rg::RenderGraphBuilder builder;
    rg::RenderPassToggleRegistry toggles;
    Registry scene;
    detail::FrameContext context;
    rg::BoundedOnceSet<32> failures;
};

Registration PreOpaqueRegistration(PreOpaqueCallback callback, Gate gate = {})
{
    Registration registration;
    registration.stage = Stage::PreOpaque;
    registration.name = "KitTest.Pass";
    registration.gate = std::move(gate);
    registration.callback = std::move(callback);
    return registration;
}

TEST(KitModuleRunTest, TheCallbackRunsWithAFilledViewAndTheFrameSerial)
{
    Runner runner;
    View seen;
    std::uint64_t serial = 0;
    const Registration registration = PreOpaqueRegistration([&](PreOpaqueFrame& f) {
        seen = f.View();
        serial = f.Serial();
    });

    EXPECT_TRUE(runner.Run(registration, GameView(), 4));
    EXPECT_EQ(seen.scope, rg::ViewScope::Primary);
    EXPECT_EQ(seen.extent.width, 64u);
    EXPECT_EQ(seen.extent.height, 32u);
    EXPECT_FLOAT_EQ(seen.eye.y, 2.0f);
    EXPECT_EQ(serial, FrameSerial(4, rg::ViewScope::Primary));
}

TEST(KitModuleRunTest, ADisabledGateSkipsTheCallback)
{
    Runner runner;
    int calls = 0;
    const Registration gated = PreOpaqueRegistration([&](PreOpaqueFrame&) { ++calls; }, Gate{ "KitTest.Pass.Draw" });

    EXPECT_TRUE(runner.Run(gated, GameView(), 1));
    runner.toggles.SetEnabled("KitTest.Pass.Draw", false);
    EXPECT_FALSE(runner.Run(gated, GameView(), 2));
    EXPECT_EQ(calls, 1);

    // The default gate is the registered name.
    const Registration byName = PreOpaqueRegistration([&](PreOpaqueFrame&) { ++calls; });
    runner.toggles.SetEnabled("KitTest.Pass", false);
    EXPECT_FALSE(runner.Run(byName, GameView(), 3));
    EXPECT_EQ(calls, 1);
}

TEST(KitModuleRunTest, APerViewCallbackNeedsAKnownNonSharedView)
{
    Runner runner;
    int calls = 0;
    const Registration registration = PreOpaqueRegistration([&](PreOpaqueFrame&) { ++calls; });

    EXPECT_FALSE(runner.Run(registration, std::nullopt, 1));
    View shared = GameView();
    shared.scope = rg::ViewScope::Shared;
    EXPECT_FALSE(runner.Run(registration, shared, 1));
    EXPECT_EQ(calls, 0);
}

TEST(KitModuleRunTest, AThrowingCallbackIsLoggedOnceAndTheFrameSurvives)
{
    Runner runner;
    RecordingLogSink sink;
    const ScopedLogSinkInstall logGuard(&sink);
    const Registration registration =
        PreOpaqueRegistration([](PreOpaqueFrame&) { throw std::runtime_error("boom"); });

    EXPECT_FALSE(runner.Run(registration, GameView(), 1));
    EXPECT_FALSE(runner.Run(registration, GameView(), 2));
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(KitModuleRunTest, ASharedCallbackRunsOncePerFrameForTwoViews)
{
    Runner runner;
    int calls = 0;
    Registration registration;
    registration.stage = Stage::PreOpaqueShared;
    registration.name = "KitTest.Shared";
    registration.callback = SharedCallback([&](SharedFrame& f) {
        ++calls;
        EXPECT_EQ(f.Scope(), rg::ViewScope::Shared);
    });
    SharedOncePerFrame latch;

    View sceneView = GameView();
    sceneView.scope = rg::ViewScope::Secondary;
    EXPECT_TRUE(runner.Run(registration, GameView(), 7, &latch));
    EXPECT_FALSE(runner.Run(registration, sceneView, 7, &latch));
    EXPECT_EQ(calls, 1);

    EXPECT_TRUE(runner.Run(registration, GameView(), 8, &latch));
    EXPECT_EQ(calls, 2);
}

TEST(KitModuleRunTest, AThrowingSharedCallbackIsNotRetriedInTheSameFrame)
{
    Runner runner;
    int calls = 0;
    Registration registration;
    registration.stage = Stage::PreOpaqueShared;
    registration.name = "KitTest.Shared";
    registration.callback = SharedCallback([&](SharedFrame&) {
        ++calls;
        throw std::runtime_error("boom");
    });
    SharedOncePerFrame latch;

    EXPECT_FALSE(runner.Run(registration, GameView(), 3, &latch));
    EXPECT_FALSE(runner.Run(registration, GameView(), 3, &latch));
    EXPECT_EQ(calls, 1);
}

TEST(KitModuleRunTest, FrameSerialsAreNonZeroAndDifferPerFrameAndView)
{
    EXPECT_NE(FrameSerial(0, rg::ViewScope::Shared), 0u);
    EXPECT_NE(FrameSerial(5, rg::ViewScope::Primary), FrameSerial(5, rg::ViewScope::Secondary));
    EXPECT_NE(FrameSerial(5, rg::ViewScope::Primary), FrameSerial(6, rg::ViewScope::Primary));
    EXPECT_NE(FrameSerial(5, rg::ViewScope::Shared), FrameSerial(5, rg::ViewScope::Primary));
}

// --- Headless Core: the real registration path --------------------------------------------------

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel, std::string_view) override { }
};

struct CountingFeature final : Feature {
    explicit CountingFeature(int& calls)
        : m_calls(calls)
    {
    }

    const char* Name() const override { return "KitModuleTest"; }

    void Setup(FeatureSetup& setup) override
    {
        setup.PreOpaque("KitModuleTest.Pre", 3, [this](PreOpaqueFrame&) { ++m_calls; });
        setup.PostOpaque("KitModuleTest.Post", 4, [this](PostOpaqueFrame&) { ++m_calls; });
        setup.PostComposite("KitModuleTest.Composite", 5, Blend::AlphaOver, [this](CompositeFrame&) { ++m_calls; });
    }

    int& m_calls;
};

std::unique_ptr<Core> TryMakeCore(HeadlessSurfaceProvider& surface, NoopHostServices& services)
{
    try {
        return std::make_unique<Core>(surface, services);
    } catch (const std::exception&) {
        return nullptr;
    }
}

bool HasEntry(const std::vector<RenderFeatureDebugEntry>& snapshot, const std::string& name)
{
    for (const RenderFeatureDebugEntry& entry : snapshot) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

TEST(KitModuleCoreTest, AFeatureRegistersItsPassesAndAnInertCallbackSurvivesTheModule)
{
    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core = TryMakeCore(surface, services);
    if (core == nullptr) {
        GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
    }

    int calls = 0;
    ProjectPreOpaqueCallback preOpaque;
    {
        std::unique_ptr<IEngineFeatureModule> module = detail::MakeModule(
            *core, "KitModuleTest", [&calls](FeatureHost&) { return std::make_unique<CountingFeature>(calls); });
        ASSERT_NE(module, nullptr);
        EXPECT_STREQ(module->ModuleName(), "KitModuleTest");

        RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
        ASSERT_NE(compositor, nullptr);
        const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
        EXPECT_TRUE(HasEntry(snapshot, "KitModuleTest.Pre"));
        EXPECT_TRUE(HasEntry(snapshot, "KitModuleTest.Post"));
        EXPECT_TRUE(HasEntry(snapshot, "KitModuleTest.Composite"));

        for (const auto& entry : compositor->StageFeaturesInPriorityOrder<CompositorStage::PreOpaque>()) {
            if (entry.name == "KitModuleTest.Pre") {
                preOpaque = entry.callback;
            }
        }
        ASSERT_TRUE(static_cast<bool>(preOpaque));

        // The engine has no view data outside a frame build: a per-view callback must not run.
        rg::RenderGraphBuilder builder;
        rg::RenderPassBlackboard blackboard;
        preOpaque(builder, blackboard, rg::RenderViewId::Named("Primary"));
        EXPECT_EQ(calls, 0);
    }

    // Core keeps the registered callback after the module is gone; it must do nothing.
    rg::RenderGraphBuilder builder;
    rg::RenderPassBlackboard blackboard;
    preOpaque(builder, blackboard, rg::RenderViewId::Named("Primary"));
    EXPECT_EQ(calls, 0);
}

} // namespace
} // namespace gte::fx
