// Tier-1 tests for the feature kit shell: view and frame values, the toggle gate, callback policy, the
// collect-then-commit setup and the lifetime guard. No Core, no GPU.

#include "Core/FeatureKit/FeatureKit.h"
#include "ECS/Registry.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
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
    view.scope = rg::ViewScope::GameView;
    const PostOpaqueFrame frame(view, FakeScene(), 7, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 });

    const Texture color = frame.SceneColor();
    const Texture depth = frame.SceneDepth();
    EXPECT_EQ(color.handle, (rg::TextureHandle{ 1, 1 }));
    EXPECT_EQ(depth.handle, (rg::TextureHandle{ 2, 1 }));
    EXPECT_EQ(color.frameSerial, 7u);
    EXPECT_TRUE(frame.Owns(color));
    EXPECT_EQ(frame.Scope(), rg::ViewScope::GameView);
}

TEST(FxFrameTest, ATextureFromAnotherFrameIsNotOwned)
{
    const PostOpaqueFrame first(View{}, FakeScene(), 1, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 });
    const PostOpaqueFrame second(View{}, FakeScene(), 2, rg::TextureHandle{ 1, 1 }, rg::TextureHandle{ 2, 1 });

    EXPECT_FALSE(second.Owns(first.SceneColor()));
    EXPECT_FALSE(second.Owns(Texture{}));
}

TEST(FxFrameTest, AnInvalidHandleIsNeverAValidTexture)
{
    const PostOpaqueFrame frame(View{}, FakeScene(), 3, rg::TextureHandle{}, rg::TextureHandle{});
    EXPECT_FALSE(frame.SceneColor().IsValid());
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
    const PassEnabledFn isEnabled = [&](std::string_view name) {
        asked.emplace_back(name);
        return true;
    };
    EXPECT_TRUE(GateAllows(Gate{}, "Effect.Mask", isEnabled));
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0], "Effect.Mask");
}

TEST(FxGateTest, ADisabledPassBlocksTheCallback)
{
    const PassEnabledFn isEnabled = [](std::string_view name) { return name != "Effect.Mask.Dispatch"; };
    EXPECT_FALSE(GateAllows(Gate{ "Effect.Mask.Dispatch" }, "Effect.Mask", isEnabled));
}

TEST(FxGateTest, AListedGateRunsWhenAnyPassIsEnabled)
{
    const PassEnabledFn onlyB = [](std::string_view name) { return name == "B.Pass"; };
    EXPECT_TRUE(GateAllows(Gate{ "A.Pass", "B.Pass" }, "Registered", onlyB));

    const PassEnabledFn none = [](std::string_view) { return false; };
    EXPECT_FALSE(GateAllows(Gate{ "A.Pass", "B.Pass" }, "Registered", none));
}

TEST(FxGateTest, NoToggleRegistryMeansEverythingRuns)
{
    EXPECT_TRUE(GateAllows(Gate{ "A.Pass" }, "Registered", PassEnabledFn{}));
}

// --- Callback policy -----------------------------------------------------------

TEST(FxCallbackPolicyTest, PerViewCallbacksNeedAKnownNonSharedView)
{
    View game;
    game.scope = rg::ViewScope::GameView;
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

} // namespace
} // namespace gte::fx
