// Tier-1 tests for the kit's data flow: channels, scene slots, scene uniforms and the gated lazy build.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "Core/FeatureKit/GatedBuild.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/RenderPipeline.h"

#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace gte::fx {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

Registry& FakeScene()
{
    static Registry registry;
    return registry;
}

// A frame with a blackboard but no builder: enough for data flow, no GPU.
struct Harness {
    explicit Harness(rg::RenderViewId viewId = rg::RenderViewId::Named("Game"), std::uint64_t serial = 5)
    {
        context.blackboard = &blackboard;
        context.view = viewId;
        context.featureName = "Feature";
        context.serial = serial;
        context.channels = &names;
    }

    PreOpaqueFrame MakeFrame(std::uint64_t serial = 5) { return PreOpaqueFrame(View{}, FakeScene(), serial, &context); }

    rg::RenderPassBlackboard blackboard;
    detail::ChannelNames names;
    detail::FrameContext context;
};

TEST(KitChannelKeyTest, StablePerFeatureChannelAndView)
{
    const rg::RenderViewId game = rg::RenderViewId::Named("Game");
    const rg::RenderViewId scene = rg::RenderViewId::Named("Scene");
    EXPECT_EQ(ChannelKey("Shadow", "Map", game), ChannelKey("Shadow", "Map", game));
    EXPECT_NE(ChannelKey("Shadow", "Map", game), ChannelKey("Shadow", "Map", scene));
    EXPECT_NE(ChannelKey("Shadow", "Map", game), ChannelKey("Shadow", "Mask", game));
    EXPECT_NE(ChannelKey("Shadow", "Map", game), ChannelKey("Other", "Map", game));
}

TEST(KitChannelKeyTest, LongNamesHashLikeShortOnes)
{
    const rg::RenderViewId game = rg::RenderViewId::Named("Game");
    const std::string longName(200, 'x');
    EXPECT_EQ(ChannelKey("F", longName, game), ChannelKey("F", longName, game));
    EXPECT_NE(ChannelKey("F", longName, game), ChannelKey("F", longName + "y", game));
}

TEST(KitChannelKeyTest, MatchesTheEngineKeyOfTheJoinedName)
{
    const rg::RenderViewId game = rg::RenderViewId::Named("Game");
    EXPECT_EQ(ChannelKey("Shadow", "Map", game), rg::SceneResourceKeyOf("Shadow.Map", game).hash);
}

TEST(KitChannelNamesTest, KnownAddedClashAndFull)
{
    detail::ChannelNames names;
    EXPECT_EQ(names.Note(1, "F", "A"), detail::ChannelNames::Result::Added);
    EXPECT_EQ(names.Note(1, "F", "A"), detail::ChannelNames::Result::Known);
    EXPECT_EQ(names.Note(1, "F", "B"), detail::ChannelNames::Result::Clash);

    for (std::uint64_t hash = 2; names.Size() < detail::ChannelNames::kCapacity; ++hash) {
        names.Note(hash, "F", std::to_string(hash));
    }
    EXPECT_EQ(names.Note(1000, "F", "Late"), detail::ChannelNames::Result::Full);
}

TEST(KitChannelTest, PublishedValueIsFetchedInTheSameView)
{
    Harness harness;
    PreOpaqueFrame frame = harness.MakeFrame();
    Channel<int> channel("Number");

    EXPECT_FALSE(channel.Fetch(frame).has_value());
    channel.Publish(frame, 42);
    ASSERT_TRUE(channel.Fetch(frame).has_value());
    EXPECT_EQ(*channel.Fetch(frame), 42);
}

TEST(KitChannelTest, AnotherViewDoesNotSeeTheValue)
{
    Harness game(rg::RenderViewId::Named("Game"));
    Channel<int> channel("Number");
    PreOpaqueFrame gameFrame = game.MakeFrame();
    channel.Publish(gameFrame, 7);

    // Same blackboard, other view.
    detail::FrameContext sceneContext = game.context;
    sceneContext.view = rg::RenderViewId::Named("Scene");
    const PreOpaqueFrame sceneFrame(View{}, FakeScene(), 5, &sceneContext);
    EXPECT_FALSE(channel.Fetch(sceneFrame).has_value());
    EXPECT_TRUE(channel.Fetch(gameFrame).has_value());
}

TEST(KitChannelTest, WrongTypeIsLoggedOnceAndReturnsNothing)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Harness harness;
    PreOpaqueFrame frame = harness.MakeFrame();
    Channel<int>("Value").Publish(frame, 1);

    const Channel<std::string> wrong("Value");
    EXPECT_FALSE(wrong.Fetch(frame).has_value());
    EXPECT_FALSE(wrong.Fetch(frame).has_value());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(KitChannelTest, ATextureFromAnotherFrameIsRefusedAndLoggedOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Harness harness;
    PreOpaqueFrame frame = harness.MakeFrame(5);
    const Texture foreign{ rg::TextureHandle{ 1, 1 }, 99 };

    Channel<int> channel("Number");
    channel.Publish(frame, 3, { foreign });
    channel.Publish(frame, 4, { foreign });
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
    EXPECT_EQ(*channel.Fetch(frame), 4);
}

TEST(KitChannelTest, AFrameWithoutAnEngineContextIsALoggedNoOp)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    PreOpaqueFrame frame(View{}, FakeScene(), 1);
    Channel<int> channel("Number");
    channel.Publish(frame, 1);
    EXPECT_FALSE(channel.Fetch(frame).has_value());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(KitSceneSlotTest, OnlyDepth2DIsADepthResource)
{
    static_assert(IsDepthSlotKind(SceneSlotKind::Depth2D));
    static_assert(!IsDepthSlotKind(SceneSlotKind::Image2D));
    static_assert(!IsDepthSlotKind(SceneSlotKind::Image3D));
    EXPECT_FALSE(SceneSlot{}.IsValid());
    EXPECT_TRUE((SceneSlot{ 0, SceneSlotKind::Depth2D }.IsValid()));
    EXPECT_FALSE((SceneSlot{ kSceneSlotCount, SceneSlotKind::Image2D }.IsValid()));
}

class FakeRegistrar final : public ISceneSlotRegistrar {
public:
    SceneSlot Declare(std::string_view name, SceneSlotKind kind, std::uint32_t preferredIndex) override
    {
        lastName = std::string(name);
        lastKind = kind;
        return SceneSlot{ preferredIndex, kind };
    }

    std::string lastName;
    SceneSlotKind lastKind = SceneSlotKind::Image2D;
};

TEST(KitSceneSlotTest, SetupWithoutARegistrarReturnsAnInvalidSlotAndLogsOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    FeatureSetup setup;
    EXPECT_FALSE(setup.DeclareSceneSlot("Map", SceneSlotKind::Depth2D, 0).IsValid());
    EXPECT_FALSE(setup.DeclareSceneSlot("Map", SceneSlotKind::Depth2D, 0).IsValid());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(KitSceneSlotTest, SetupForwardsToItsRegistrar)
{
    FakeRegistrar registrar;
    FeatureSetup setup(&registrar);
    const SceneSlot slot = setup.DeclareSceneSlot("Map", SceneSlotKind::Depth2D, 3);
    EXPECT_TRUE(slot.IsValid());
    EXPECT_EQ(slot.index, 3u);
    EXPECT_EQ(registrar.lastName, "Map");
    EXPECT_EQ(registrar.lastKind, SceneSlotKind::Depth2D);
}

TEST(KitSceneSlotTest, PublishToSceneRefusesAnInvalidSlotAndAnInvalidFrame)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    Harness harness;
    PreOpaqueFrame frame = harness.MakeFrame(5);
    frame.PublishToScene(SceneSlot{}, frame.Stamp(rg::TextureHandle{ 1, 1 }));
    frame.PublishToScene(SceneSlot{ 0, SceneSlotKind::Image2D }, Texture{ rg::TextureHandle{ 1, 1 }, 99 });
    EXPECT_GE(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

struct Uniforms112 {
    float values[28];
};

TEST(KitSceneUniformsTest, ASmallTriviallyCopyableBlockIsAccepted)
{
    static_assert(sizeof(Uniforms112) <= kMaxSceneUniformBytes);
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    PreOpaqueFrame frame(View{}, FakeScene(), 1);
    frame.SetSceneUniforms(Uniforms112{});
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1); // No engine context: a logged no-op.
}

TEST(KitGatedBuildTest, SuccessIsFinal)
{
    detail::GatedBuild build;
    int calls = 0;
    EXPECT_EQ(build.Ensure(1, [&] { ++calls; }), detail::GatedBuild::Result::Ready);
    EXPECT_EQ(build.Ensure(2, [&] { ++calls; }), detail::GatedBuild::Result::Ready);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(build.Ready());
}

TEST(KitGatedBuildTest, FailureCoolsDownRetriesAndRecovers)
{
    detail::GatedBuild build;
    const auto fail = [] { throw std::runtime_error("no file"); };
    EXPECT_EQ(build.Ensure(10, fail), detail::GatedBuild::Result::FailedFirst);
    EXPECT_EQ(build.LastError(), "no file");
    EXPECT_FALSE(build.Ready());

    EXPECT_EQ(build.Ensure(11, fail), detail::GatedBuild::Result::Cooling);
    const std::uint64_t retryFrame = 10 + rg::BuildRetryGate::kInitialCooldownFrames;
    EXPECT_EQ(build.Ensure(retryFrame, fail), detail::GatedBuild::Result::FailedAgain);

    // Never gives up: a much later attempt that works recovers.
    EXPECT_EQ(build.Ensure(1'000'000, [] {}), detail::GatedBuild::Result::Recovered);
    EXPECT_TRUE(build.Ready());
}

} // namespace
} // namespace gte::fx
