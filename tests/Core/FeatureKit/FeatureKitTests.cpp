// Tier-1 tests for the feature kit shell: view and frame values, the toggle gate, callback policy, the
// collect-then-commit setup and the lifetime guard. No Core, no GPU.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "Core/Plugins/ViewScopeEnable.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace gte::fx {
namespace {

// Frames only keep a reference to the scene; the tests never read it.
Registry& FakeScene()
{
    static Registry registry;
    return registry;
}

// --- View ----------------------------------------------------------------------

TEST(FxViewTest, DefaultsAreASharedViewWithNoCamera)
{
    const View view;
    EXPECT_EQ(view.scope, rg::ViewScope::Shared);
    EXPECT_EQ(view.extent.width, 0u);
    EXPECT_EQ(view.extent.height, 0u);
    EXPECT_FALSE(view.invViewProjectionValid);
}

// --- Frame ---------------------------------------------------------------------

TEST(FxFrameTest, TexturesCarryTheSerialOfTheFrameThatMadeThem)
{
    View view;
    view.scope = rg::ViewScope::Primary;
    const PostOpaqueFrame frame(view, FakeScene(), 7, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 });

    const Texture color = frame.SceneColor();
    const Texture depth = frame.SceneDepth();
    EXPECT_EQ(color.handle, (rg::TextureHandle{ 1, 1 }));
    EXPECT_EQ(depth.handle, (rg::TextureHandle{ 2, 1 }));
    EXPECT_EQ(color.frameSerial, 7u);
    EXPECT_TRUE(frame.Owns(color));
    EXPECT_EQ(frame.Scope(), rg::ViewScope::Primary);
}

TEST(FxFrameTest, ATextureFromAnotherFrameIsNotOwned)
{
    const PostOpaqueFrame first(View{}, FakeScene(), 1, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 });
    const PostOpaqueFrame second(View{}, FakeScene(), 5, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 });

    EXPECT_FALSE(second.Owns(first.SceneColor()));
    EXPECT_FALSE(second.Owns(Texture{}));
}

TEST(FxFrameTest, AnInvalidHandleIsNeverAValidTexture)
{
    const PostOpaqueFrame frame(View{}, FakeScene(), 3, rg::TextureHandle{}, rg::TextureHandle{});
    EXPECT_FALSE(frame.SceneColor().IsValid());
}

TEST(FxFrameTest, OpaqueFrameExposesTheViewTargetStampedWithItsSerial)
{
    const OpaqueFrame frame(View{}, FakeScene(), 6, rg::TextureHandle{ 9, 1 });
    EXPECT_EQ(frame.SceneTarget().handle, (rg::TextureHandle{ 9, 1 }));
    EXPECT_TRUE(frame.Owns(frame.SceneTarget()));
}

TEST(FxFrameTest, CompositeFrameAddsThePrivateTargetAndExtent)
{
    const CompositeFrame frame(View{}, FakeScene(), 4, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 },
        rg::TextureHandle{ 3, 1 }, Extent2D{ 640, 360 });
    EXPECT_EQ(frame.PrivateTarget().handle, (rg::TextureHandle{ 3, 1 }));
    EXPECT_EQ(frame.Extent().width, 640u);
    EXPECT_EQ(frame.SceneColor().handle, (rg::TextureHandle{ 1, 1 })); // It is also a PostOpaqueFrame.
}

TEST(FxFrameTest, SharedFrameHasTheSharedScope)
{
    const SharedFrame frame(FakeScene(), 5);
    EXPECT_EQ(frame.Scope(), rg::ViewScope::Shared);
}

// --- Gate ----------------------------------------------------------------------

TEST(FxGateTest, EmptyGateAsksForTheRegisteredName)
{
    std::vector<std::string> asked;
    const auto isEnabled = [&](std::string_view name) {
        asked.emplace_back(name);
        return true;
    };
    EXPECT_TRUE(GateAllows(Gate{}, "Effect.Mask", isEnabled));
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0], "Effect.Mask");
}

TEST(FxGateTest, ADisabledPassBlocksTheCallback)
{
    const auto isEnabled = [](std::string_view name) { return name != "Effect.Mask.Dispatch"; };
    EXPECT_FALSE(GateAllows(Gate{ "Effect.Mask.Dispatch" }, "Effect.Mask", isEnabled));
}

TEST(FxGateTest, AListedGateRunsWhenAnyPassIsEnabled)
{
    const auto onlyB = [](std::string_view name) { return name == "B.Pass"; };
    EXPECT_TRUE(GateAllows(Gate{ "A.Pass", "B.Pass" }, "Registered", onlyB));

    const auto none = [](std::string_view) { return false; };
    EXPECT_FALSE(GateAllows(Gate{ "A.Pass", "B.Pass" }, "Registered", none));
}

TEST(FxGateTest, AnAlwaysEnabledPredicateLetsEverythingRun)
{
    EXPECT_TRUE(GateAllows(Gate{ "A.Pass" }, "Registered", [](std::string_view) { return true; }));
}

// --- Shared once per frame -----------------------------------------------------

TEST(FxSharedOncePerFrameTest, RunsOnceThenSkipsTheRestOfTheFrame)
{
    SharedOncePerFrame latch;
    EXPECT_TRUE(latch.ShouldRun(1));
    latch.MarkDone(1);
    EXPECT_FALSE(latch.ShouldRun(1)); // The second view of frame 1.
    EXPECT_TRUE(latch.ShouldRun(2));
}

TEST(FxSharedOncePerFrameTest, FrameZeroIsAValidFirstFrame)
{
    SharedOncePerFrame latch;
    EXPECT_TRUE(latch.ShouldRun(0));
    latch.MarkDone(0);
    EXPECT_FALSE(latch.ShouldRun(0));
}

TEST(FxSharedOncePerFrameTest, AFailedCallbackIsNotRetriedInTheSameFrame)
{
    SharedOncePerFrame latch;
    latch.MarkFailed(5);
    EXPECT_FALSE(latch.ShouldRun(5));
    EXPECT_TRUE(latch.ShouldRun(6));
}

TEST(FxSharedOncePerFrameTest, NothingIsMarkedUntilTheCallbackFinished)
{
    SharedOncePerFrame latch;
    EXPECT_TRUE(latch.ShouldRun(3)); // Asking does not mark.
    EXPECT_TRUE(latch.ShouldRun(3));
}

// --- Callback policy -----------------------------------------------------------

TEST(FxCallbackPolicyTest, PerViewCallbacksNeedAKnownNonSharedView)
{
    View game;
    game.scope = rg::ViewScope::Primary;
    View shared;
    shared.scope = rg::ViewScope::Shared;

    EXPECT_TRUE(ShouldInvokeFeatureCallback(game, CallbackScope::PerView));
    EXPECT_FALSE(ShouldInvokeFeatureCallback(shared, CallbackScope::PerView));
    EXPECT_FALSE(ShouldInvokeFeatureCallback(std::nullopt, CallbackScope::PerView));
}

TEST(FxCallbackPolicyTest, SharedCallbacksIgnoreTheView)
{
    EXPECT_TRUE(ShouldInvokeFeatureCallback(std::nullopt, CallbackScope::Shared));
    View shared;
    EXPECT_TRUE(ShouldInvokeFeatureCallback(shared, CallbackScope::Shared));
}

// --- FeatureSetup --------------------------------------------------------------

TEST(FxFeatureSetupTest, CollectsRegistrationsInOrderWithTheirFields)
{
    FeatureSetup setup;
    setup.PreOpaque("Effect.Depth", 10, [](PreOpaqueFrame&) {});
    setup.PostOpaque("Effect.Mask", 0, [](PostOpaqueFrame&) {}, Gate{ "Effect.Mask.Dispatch" });
    setup.PostComposite("Effect.Composite", -500, Blend::ScreenSpaceMask, [](CompositeFrame&) {});

    ASSERT_TRUE(setup.Errors().empty());
    ASSERT_EQ(setup.Registrations().size(), 3u);
    EXPECT_EQ(setup.Registrations()[0].stage, Stage::PreOpaque);
    EXPECT_EQ(setup.Registrations()[0].priority, 10);
    EXPECT_EQ(setup.Registrations()[1].stage, Stage::PostOpaque);
    ASSERT_EQ(setup.Registrations()[1].gate.passNames.size(), 1u);
    EXPECT_EQ(setup.Registrations()[2].blend, Blend::ScreenSpaceMask);
    EXPECT_EQ(setup.Registrations()[2].name, "Effect.Composite");
}

TEST(FxFeatureSetupTest, EveryStageHasItsVerb)
{
    FeatureSetup setup;
    setup.PostTransparent("Late", 0, [](PostOpaqueFrame&) {});
    setup.PreUI("Ui", 0, Blend::AlphaOver, [](CompositeFrame&) {});
    setup.PreOpaqueShared("Once", 0, [](SharedFrame&) {});

    ASSERT_EQ(setup.Registrations().size(), 3u);
    EXPECT_EQ(setup.Registrations()[0].stage, Stage::PostTransparent);
    EXPECT_EQ(setup.Registrations()[1].stage, Stage::PreUI);
    EXPECT_EQ(setup.Registrations()[2].stage, Stage::PreOpaqueShared);
}

TEST(FxFeatureSetupTest, RegistrationsStartEnabledInTheSecondaryViewUnlessTheDefaultIsChanged)
{
    FeatureSetup setup;
    setup.PreOpaque("A", 0, [](PreOpaqueFrame&) {});
    setup.DefaultSecondaryViewEnabled(false);
    setup.PostOpaque("B", 0, [](PostOpaqueFrame&) {});
    setup.PostComposite("C", 0, Blend::AlphaOver, [](CompositeFrame&) {});
    setup.DefaultSecondaryViewEnabled(true);
    setup.PostTransparent("D", 0, [](PostOpaqueFrame&) {});

    ASSERT_TRUE(setup.Errors().empty());
    ASSERT_EQ(setup.Registrations().size(), 4u);
    EXPECT_TRUE(setup.Registrations()[0].secondaryViewDefault);
    EXPECT_FALSE(setup.Registrations()[1].secondaryViewDefault);
    EXPECT_FALSE(setup.Registrations()[2].secondaryViewDefault);
    EXPECT_TRUE(setup.Registrations()[3].secondaryViewDefault);
}

// --- ViewScopeEnable -------------------------------------------------------------

TEST(ViewScopeEnableTest, FlagsDefaultOnAndSetOnlyTouchesTheNamedScope)
{
    ViewScopeEnable flags;
    EXPECT_TRUE(flags.Allows(rg::ViewScope::Primary));
    EXPECT_TRUE(flags.Allows(rg::ViewScope::Secondary));

    flags.Set(rg::ViewScope::Secondary, false);
    EXPECT_TRUE(flags.Allows(rg::ViewScope::Primary));
    EXPECT_FALSE(flags.Allows(rg::ViewScope::Secondary));

    flags.Set(rg::ViewScope::Shared, false); // No flag for the shared scope: nothing changes.
    EXPECT_TRUE(flags.primary);
    EXPECT_FALSE(flags.secondary);
}

TEST(ViewScopeEnableTest, TheSharedScopeIsNeverGated)
{
    ViewScopeEnable off;
    off.primary = false;
    off.secondary = false;
    EXPECT_TRUE(off.Allows(rg::ViewScope::Shared));
}

TEST(ViewScopeEnableTest, RunsInScopeIsGlobalAndScopeFlag)
{
    for (const bool global : { false, true }) {
        for (const bool primary : { false, true }) {
            for (const bool secondary : { false, true }) {
                ViewScopeEnable flags;
                flags.primary = primary;
                flags.secondary = secondary;
                EXPECT_EQ(RunsInScope(global, flags, rg::ViewScope::Primary), global && primary);
                EXPECT_EQ(RunsInScope(global, flags, rg::ViewScope::Secondary), global && secondary);
                EXPECT_EQ(RunsInScope(global, flags, rg::ViewScope::Shared), global);
            }
        }
    }
}

TEST(FxFeatureSetupTest, EmptyLongDuplicateAndCallbacklessRegistrationsAreErrors)
{
    FeatureSetup setup;
    setup.PreOpaque("", 0, [](PreOpaqueFrame&) {});
    setup.PreOpaque(std::string(kMaxRegistrationNameBytes + 1, 'x'), 0, [](PreOpaqueFrame&) {});
    setup.PreOpaque("Same", 0, [](PreOpaqueFrame&) {});
    setup.PreOpaque("Same", 1, [](PreOpaqueFrame&) {});
    setup.PreOpaque("NoCallback", 0, PreOpaqueCallback{});

    EXPECT_EQ(setup.Errors().size(), 4u);
    EXPECT_EQ(setup.Registrations().size(), 1u); // Only the first "Same".
}

TEST(FxFeatureSetupTest, ANameOfExactlyTheLimitIsAccepted)
{
    FeatureSetup setup;
    setup.PreOpaque(std::string(kMaxRegistrationNameBytes, 'x'), 0, [](PreOpaqueFrame&) {});
    EXPECT_TRUE(setup.Errors().empty());
}

TEST(FxFeatureSetupTest, OpaqueRegistrationIsAcceptedAndKeepsItsStage)
{
    FeatureSetup setup;
    setup.Opaque("Effect.Opaque", 5, [](OpaqueFrame&) {});
    setup.Opaque("", 0, [](OpaqueFrame&) {});
    setup.Opaque("Effect.Opaque", 1, [](OpaqueFrame&) {});
    setup.Opaque("NoCallback", 0, OpaqueCallback{});

    EXPECT_EQ(setup.Errors().size(), 3u);
    ASSERT_EQ(setup.Registrations().size(), 1u);
    EXPECT_EQ(setup.Registrations()[0].stage, Stage::Opaque);
    EXPECT_EQ(setup.Registrations()[0].priority, 5);
    EXPECT_TRUE(std::holds_alternative<OpaqueCallback>(setup.Registrations()[0].callback));
}

TEST(FxFeatureSetupTest, EmptyViewChannelReaderIsRecordedOnItsRegistration)
{
    FeatureSetup setup;
    setup.PostOpaque("Effect.Reader", 0, [](PostOpaqueFrame&) {});
    setup.PostOpaque("Effect.Other", 0, [](PostOpaqueFrame&) {});
    setup.ReadsChannelInEmptyViews("Effect.Reader", "Owner", "Data");

    EXPECT_TRUE(setup.Errors().empty());
    ASSERT_EQ(setup.Registrations().size(), 2u);
    EXPECT_EQ(setup.Registrations()[0].consumedChannel, "Owner.Data");
    EXPECT_TRUE(setup.Registrations()[1].consumedChannel.empty());

    setup.ReadsChannelInEmptyViews("Missing", "Owner", "Data");
    EXPECT_EQ(setup.Errors().size(), 1u);
}

// --- SetupAndCommit ------------------------------------------------------------

class RecordingSink final : public IRegistrationSink {
public:
    void Commit(const Registration& registration) override
    {
        if (failOn == registration.name) {
            throw std::runtime_error("refused " + registration.name);
        }
        committed.push_back(registration.name);
    }

    std::vector<std::string> committed;
    std::string failOn;
};

class TestFeature final : public Feature {
public:
    const char* Name() const override { return "Test"; }

    void Setup(FeatureSetup& setup) override
    {
        setup.PreOpaque("First", 0, [](PreOpaqueFrame&) {});
        if (throwAfterFirst) {
            throw std::runtime_error("setup failed");
        }
        setup.PostOpaque("Second", 0, [](PostOpaqueFrame&) {});
        if (addBadRegistration) {
            setup.PostOpaque("", 0, [](PostOpaqueFrame&) {});
        }
    }

    bool throwAfterFirst = false;
    bool addBadRegistration = false;
};

TEST(FxSetupAndCommitTest, SuccessCommitsEveryRegistrationInOrder)
{
    TestFeature feature;
    RecordingSink sink;
    EXPECT_FALSE(SetupAndCommit(feature, sink).has_value());
    EXPECT_EQ(sink.committed, (std::vector<std::string>{ "First", "Second" }));
}

TEST(FxSetupAndCommitTest, AThrowingSetupCommitsNothing)
{
    TestFeature feature;
    feature.throwAfterFirst = true;
    RecordingSink sink;
    const std::optional<std::string> failure = SetupAndCommit(feature, sink);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("setup failed"), std::string::npos);
    EXPECT_TRUE(sink.committed.empty());
}

TEST(FxSetupAndCommitTest, AnInvalidRegistrationCommitsNothing)
{
    TestFeature feature;
    feature.addBadRegistration = true;
    RecordingSink sink;
    const std::optional<std::string> failure = SetupAndCommit(feature, sink);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("empty name"), std::string::npos);
    EXPECT_TRUE(sink.committed.empty());
}

TEST(FxSetupAndCommitTest, ASinkFailureStopsTheCommitAndIsReported)
{
    TestFeature feature;
    RecordingSink sink;
    sink.failOn = "Second";
    const std::optional<std::string> failure = SetupAndCommit(feature, sink);
    ASSERT_TRUE(failure.has_value());
    EXPECT_NE(failure->find("refused Second"), std::string::npos);
    EXPECT_EQ(sink.committed, (std::vector<std::string>{ "First" }));
}

// --- WhileAlive ----------------------------------------------------------------

TEST(FxWhileAliveTest, RunsWhileTheLifeTokenExistsAndDoesNothingAfter)
{
    int calls = 0;
    int lastArgument = 0;
    auto life = std::make_shared<FeatureLife>();
    const WhileAlive guarded(std::weak_ptr<FeatureLife>(life), [&](int argument) {
        ++calls;
        lastArgument = argument;
    });

    guarded(41);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(lastArgument, 41);

    life.reset(); // The module is destroyed.
    guarded(42);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(lastArgument, 41);
}

TEST(FxWhileAliveTest, ConvertsToAStdFunctionOfTheSameShape)
{
    auto life = std::make_shared<FeatureLife>();
    int calls = 0;
    std::function<void(int, const std::string&)> callback =
        WhileAlive(std::weak_ptr<FeatureLife>(life), [&](int, const std::string&) { ++calls; });
    callback(1, "x");
    life.reset();
    callback(2, "y");
    EXPECT_EQ(calls, 1);
}

// --- Verbs ---------------------------------------------------------------------

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

static_assert(!std::is_convertible_v<std::string, rg::StaticName>, "pass names must be literals");
static_assert(!std::is_convertible_v<const char*, rg::StaticName>, "pass names must be literals");

TEST(FxVerbTest, DepthWrapsTheSameTexture)
{
    const Texture texture{ rg::TextureHandle{ 4, 1 }, 9 };
    EXPECT_EQ(Depth(texture).texture.handle, texture.handle);
    EXPECT_EQ(Depth(texture).texture.frameSerial, 9u);
}

TEST(FxVerbTest, PersistentNameAddsTheViewSuffix)
{
    EXPECT_EQ(PersistentName("Mask", rg::ViewScope::Primary, "GameView"), "Mask.GameView");
    EXPECT_EQ(PersistentName("Mask", rg::ViewScope::Secondary, "SceneView"), "Mask.SceneView");
    EXPECT_EQ(PersistentName("Mask", rg::ViewScope::Shared, "GameView"), "Mask");
}

TEST(FxVerbTest, VerbsOnAFrameWithoutContextAreInertAndLogOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    ComputeShader compute("DoesNotExist");
    FullscreenShader fullscreen("DoesNotExist");
    GpuParams<float> params;
    PreOpaqueFrame frame(View{}, FakeScene(), 1);

    EXPECT_FALSE(frame.Compute(compute, "Test.Dispatch").Params(params, 1.0f).Bind("a", Texture{}).Push(2).Dispatch(Extent2D{ 8, 8 }));
    EXPECT_FALSE(frame.Fullscreen(fullscreen, "Test.Draw").Bind("a", Depth(Texture{})).Target(Texture{}).Draw());
    frame.Keep(Texture{});
    EXPECT_FALSE(frame.Blit("Test.Blit", Texture{}, Texture{}, BlitFilter::Linear));
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

// A frame with a context over a bare builder: no renderer, so shaders stay inert and verbs declare no pass.
struct BareContext {
    BareContext()
    {
        context.builder = &builder;
        context.scope = rg::ViewScope::Primary;
        context.event = rg::RenderPassEvent::AfterOpaques;
        context.serial = 1;
    }

    rg::RenderGraphBuilder builder;
    detail::FrameContext context;
};

TEST(FxVerbTest, ATextureFromAnotherFrameIsRefusedOnceAndTheChainDeclaresNothing)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    BareContext bare;
    ComputeShader compute("DoesNotExist");
    PostOpaqueFrame frame(View{}, FakeScene(), 1, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 }, &bare.context);
    const Texture foreign{ rg::TextureHandle{ 3, 1 }, 99 };

    for (int i = 0; i < 2; ++i) {
        EXPECT_FALSE(frame.Compute(compute, "Test.Dispatch").Bind("a", foreign).Dispatch(Extent2D{ 8, 8 }));
    }
    EXPECT_EQ(bare.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(FxVerbTest, ABlitIsDeclaredWithTheFramesScopeAndAFrameOwnedPair)
{
    BareContext bare;
    PostOpaqueFrame frame(View{}, FakeScene(), 1, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 }, &bare.context);

    EXPECT_TRUE(frame.Blit("Test.Blit", frame.SceneColor(), frame.Stamp(rg::TextureHandle{ 5, 1 }), BlitFilter::Nearest));
    EXPECT_EQ(bare.builder.DeclaredPassCount(), 1u);
}

TEST(FxVerbTest, ATogglePassDeclaresNothingAndStaysSilent)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    BareContext bare;
    rg::RenderPassToggleRegistry toggles;
    toggles.SetEnabled("Test.Blit", false);
    toggles.SetEnabled("Test.Dispatch", false);
    bare.context.toggles = &toggles;
    ComputeShader compute("DoesNotExist");
    PostOpaqueFrame frame(View{}, FakeScene(), 1, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 }, &bare.context);

    EXPECT_FALSE(frame.Blit("Test.Blit", frame.SceneColor(), frame.SceneDepth(), BlitFilter::Linear));
    EXPECT_FALSE(frame.Compute(compute, "Test.Dispatch").Bind("a", Texture{}).Dispatch(Extent2D{ 8, 8 }));
    EXPECT_EQ(bare.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 0);
}

TEST(FxVerbTest, AMovedFromChainIsSafe)
{
    BareContext bare;
    ComputeShader compute("DoesNotExist");
    PostOpaqueFrame frame(View{}, FakeScene(), 1, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 }, &bare.context);

    ComputeChain first = frame.Compute(compute, "Test.Dispatch");
    ComputeChain second = std::move(first);
    EXPECT_FALSE(first.Bind("a", frame.SceneColor()).Dispatch(Extent2D{ 8, 8 }));
    EXPECT_FALSE(second.Dispatch(Extent2D{ 8, 8 }));
}

} // namespace
} // namespace gte::fx
