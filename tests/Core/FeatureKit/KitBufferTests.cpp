// Tests: graph buffer refs, persistent buffers and the kit buffer verbs.

#include "Core/FeatureKit/FeatureKit.h"
#include "Core/FeatureKit/FrameContext.h"
#include "Core/FeatureKit/PersistentBufferPolicy.h"
#include "ECS/Registry.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RecordedPass.h"
#include "Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <numeric>
#include <optional>
#include <vector>

namespace gte::fx {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

constexpr rg::ViewScope kGame = rg::ViewScope::GameView;

Registry& FakeScene()
{
    static Registry registry;
    return registry;
}

// --- Policy ---------------------------------------------------------------------

TEST(FxBufferPolicyTest, AcquireDecisionTable)
{
    constexpr std::uint64_t kMax = kMaxPersistentBufferBytes;
    EXPECT_EQ(DecidePersistentBufferAcquire(0, 0, false), PersistentBufferDecision::RefuseEmpty);
    EXPECT_EQ(DecidePersistentBufferAcquire(0, 100, false), PersistentBufferDecision::Create);
    EXPECT_EQ(DecidePersistentBufferAcquire(256, 100, false), PersistentBufferDecision::Reuse);
    EXPECT_EQ(DecidePersistentBufferAcquire(256, 256, true), PersistentBufferDecision::Reuse);
    EXPECT_EQ(DecidePersistentBufferAcquire(256, 300, false), PersistentBufferDecision::Grow);
    EXPECT_EQ(DecidePersistentBufferAcquire(256, 300, true), PersistentBufferDecision::RefuseGrowWhilePipelined);
    EXPECT_EQ(DecidePersistentBufferAcquire(0, 300, true), PersistentBufferDecision::Create);
    EXPECT_EQ(DecidePersistentBufferAcquire(0, kMax + 1, false), PersistentBufferDecision::RefuseTooLarge);
}

TEST(FxBufferPolicyTest, CapacityIsAPowerOfTwoBetweenTheLimits)
{
    constexpr std::uint64_t kMax = kMaxPersistentBufferBytes;
    EXPECT_EQ(PersistentBufferCapacityFor(1), 256u);
    EXPECT_EQ(PersistentBufferCapacityFor(256), 256u);
    EXPECT_EQ(PersistentBufferCapacityFor(257), 512u);
    EXPECT_EQ(PersistentBufferCapacityFor(1000), 1024u);
    EXPECT_EQ(PersistentBufferCapacityFor(kMax), kMax);
    EXPECT_EQ(PersistentBufferCapacityFor(UINT64_MAX), kMax);
}

TEST(FxBufferPolicyTest, FillCoversTheRequestedSizeRoundedUpToFourBytes)
{
    EXPECT_EQ(FillByteCount(0), 0u);
    EXPECT_EQ(FillByteCount(4), 4u);
    EXPECT_EQ(FillByteCount(5), 8u);
    EXPECT_EQ(FillByteCount(8), 8u);
}

// --- Buffer refs ----------------------------------------------------------------

TEST(FxBufferRefTest, AFrameOwnsOnlyRefsOfItsOwnViewAndFrameOrTheSharedFrame)
{
    const std::uint64_t serial = FrameSerial(1, kGame);
    const Frame frame(View{}, FakeScene(), serial);
    const rg::BufferHandle handle{ 0, 1 };

    const GpuBufferRef stale{ handle, FrameSerial(0, kGame), 16, BufferUse::Storage };
    EXPECT_FALSE(frame.Owns(stale));
    EXPECT_TRUE(frame.Owns(frame.StampBuffer(handle, 16, BufferUse::Storage)));
    EXPECT_TRUE(frame.Owns(GpuBufferRef{ handle, SharedSerialOf(serial), 16, BufferUse::Storage }));
    EXPECT_FALSE(frame.Owns(GpuBufferRef{}));
    EXPECT_FALSE(GpuBufferRef{}.IsValid());
}

TEST(FxBufferRefTest, WithoutAContextEveryBufferVerbIsAnInertLoggedNoOp)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    const std::uint64_t serial = FrameSerial(1, kGame);
    PersistentBuffer buffer("Test.Items", BufferUse::Storage);

    PreOpaqueFrame acquireFrame(View{}, FakeScene(), serial);
    EXPECT_FALSE(buffer.Acquire(acquireFrame, 64).IsValid());
    EXPECT_FALSE(buffer.Acquire(acquireFrame, 64).IsValid());
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1); // A frame without a context logs once.

    PreOpaqueFrame fillFrame(View{}, FakeScene(), serial);
    const GpuBufferRef ref = fillFrame.StampBuffer(rg::BufferHandle{ 0, 1 }, 16, BufferUse::Storage);
    EXPECT_FALSE(fillFrame.FillBuffer("Test.Fill", ref, 0u));
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 2);

    ComputeShader shader("DoesNotExist");
    PreOpaqueFrame computeFrame(View{}, FakeScene(), serial);
    EXPECT_FALSE(computeFrame.Compute(shader, "Test.Pass").BindBuffer("items", ref).Dispatch(Extent2D{ 8, 1 }));
}

// --- FillBuffer on a bare builder -------------------------------------------------

struct FillScene {
    FillScene()
    {
        context.builder = &builder;
        context.scope = kGame;
        context.event = rg::RenderPassEvent::PreOpaques;
        context.serial = FrameSerial(0, kGame);
    }

    GpuBufferRef Ref(BufferUse use, std::uint64_t bytes) const { return frame.StampBuffer(handle, bytes, use); }

    rg::RenderGraphBuilder builder;
    detail::FrameContext context;
    PreOpaqueFrame frame{ View{}, FakeScene(), FrameSerial(0, kGame), &context };
    rg::BufferHandle handle = builder.CreateBuffer("B", rg::BufferDesc{ 256, BufferUsage::None });
};

TEST(FxFillBufferTest, AStaleRefIsRefused)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    FillScene scene;
    const GpuBufferRef stale{ scene.handle, FrameSerial(7, kGame), 16, BufferUse::Storage };

    EXPECT_FALSE(scene.frame.FillBuffer("Test.Fill", stale, 0u));
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);
}

TEST(FxFillBufferTest, AHostVisibleBufferNeedsTransferDst)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    FillScene scene;

    EXPECT_FALSE(scene.frame.FillBuffer("Test.Fill", scene.Ref(BufferUse::Storage | BufferUse::HostWrite, 16), 0u));
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 1);

    EXPECT_TRUE(scene.frame.FillBuffer(
        "Test.Fill2", scene.Ref(BufferUse::Storage | BufferUse::HostWrite | BufferUse::TransferDst, 16), 0u));
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 1u);
}

TEST(FxFillBufferTest, ADeviceLocalBufferIsFilledAsATransferWrite)
{
    FillScene scene;

    ASSERT_TRUE(scene.frame.FillBuffer("Test.Fill", scene.Ref(BufferUse::Storage, 16), 3u));
    const rg::CompiledGraphInput input = scene.builder.Finish();
    ASSERT_EQ(input.passes.size(), 1u);
    ASSERT_EQ(input.passes[0].writes.size(), 1u);
    EXPECT_EQ(input.passes[0].writes[0].access, rg::ResourceAccess::TransferDst);
    EXPECT_EQ(input.passes[0].writes[0].buffer, scene.handle);
}

TEST(FxFillBufferTest, AZeroSizeFillIsRefusedByTheTransferRecord)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    FillScene scene;

    EXPECT_FALSE(scene.frame.FillBuffer("Test.Fill", scene.Ref(BufferUse::Storage, 0), 0u));
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountErrorsByCategory(sink, "TransferPass"), 1);
}

TEST(FxFillBufferTest, ASizeOfFiveBytesFillsEightAndIsAccepted)
{
    FillScene scene;

    EXPECT_TRUE(scene.frame.FillBuffer("Test.Fill", scene.Ref(BufferUse::Storage, 5), 0u));
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 1u);
}

TEST(FxFillBufferTest, ATogglePassDeclaresNothingAndStaysSilent)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    FillScene scene;
    rg::RenderPassToggleRegistry toggles;
    toggles.SetEnabled("Test.Fill", false);
    scene.context.toggles = &toggles;

    EXPECT_FALSE(scene.frame.FillBuffer("Test.Fill", scene.Ref(BufferUse::Storage, 16), 0u));
    EXPECT_EQ(scene.builder.DeclaredPassCount(), 0u);
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), 0);
}

// --- Real device ------------------------------------------------------------------

constexpr rg::ExecuteTimingMode kSync = rg::ExecuteTimingMode::SynchronousImmediateReadback;

// Member order matters: the builder and the context exist before the frame.
struct TestFrame {
    TestFrame(HeadlessRenderGraphFixture& fixture, std::uint64_t frameIndex, rg::ViewScope scope)
        : frame(View{}, FakeScene(), FrameSerial(frameIndex, scope), &context)
    {
        builder.SetRenderer(&fixture.GetRenderer());
        builder.SetPersistentResourceCache(nullptr, kSync, 1);
        context.builder = &builder;
        context.scope = scope;
        context.event = rg::RenderPassEvent::PreOpaques;
        context.serial = FrameSerial(frameIndex, scope);
    }

    rg::RenderGraphBuilder builder;
    detail::FrameContext context;
    PreOpaqueFrame frame;
};

std::uint64_t SizeOf(const TestFrame& f, const GpuBufferRef& ref)
{
    const std::optional<rg::BufferDesc> desc = f.builder.DescOfBuffer(ref.handle);
    return desc.has_value() ? desc->size : 0;
}

TEST(FxPersistentBufferGpuTest, GrowsInPowersOfTwoAndImportsOncePerFrame)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    PersistentBuffer buffer("Test.Items", BufferUse::Storage | BufferUse::HostWrite);
    const std::vector<std::uint32_t> data(32, 1u);

    TestFrame f0(fixture, 0, kGame);
    const GpuBufferRef a = buffer.Acquire(f0.frame, 100);
    ASSERT_TRUE(a.IsValid());
    EXPECT_EQ(SizeOf(f0, a), 256u);
    const GpuBufferRef b = buffer.Acquire(f0.frame, 200);
    ASSERT_TRUE(b.IsValid());
    EXPECT_EQ(b.handle, a.handle);
    EXPECT_FALSE(buffer.Acquire(f0.frame, 300).IsValid()); // Cannot grow after use in this frame.
    EXPECT_TRUE(buffer.Upload(f0.frame, data.data(), 100));

    TestFrame f1(fixture, 1, kGame);
    const GpuBufferRef d = buffer.Acquire(f1.frame, 300);
    ASSERT_TRUE(d.IsValid());
    EXPECT_EQ(SizeOf(f1, d), 512u);
    EXPECT_FALSE(f1.frame.Owns(a));

    TestFrame f2(fixture, 2, kGame);
    const GpuBufferRef e = buffer.Acquire(f2.frame, 64);
    ASSERT_TRUE(e.IsValid());
    EXPECT_EQ(SizeOf(f2, e), 512u);

    TestFrame f3(fixture, 1, rg::ViewScope::SceneView); // The scene view has its own slot.
    const GpuBufferRef g = buffer.Acquire(f3.frame, 100);
    ASSERT_TRUE(g.IsValid());
    EXPECT_EQ(SizeOf(f3, g), 256u);

    // Same serial, another builder: the slot imports again for the new graph.
    TestFrame f4(fixture, 1, kGame);
    const GpuBufferRef h = buffer.Acquire(f4.frame, 300);
    ASSERT_TRUE(h.IsValid());
    EXPECT_TRUE(f4.builder.DescOfBuffer(h.handle).has_value());

    const int errorsBefore = CountErrorsByCategory(sink, "FeatureKit");
    TestFrame f5(fixture, 9, kGame);
    EXPECT_FALSE(buffer.Upload(f5.frame, data.data(), 16)); // No Acquire in this callback.
    EXPECT_EQ(CountErrorsByCategory(sink, "FeatureKit"), errorsBefore + 1);
}

// Declares a copy of a graph buffer into a host-readable buffer and keeps it alive.
void AddHostCopy(rg::RenderGraphBuilder& builder, const char* name, rg::BufferHandle src, const rg::GpuBuffer& host,
    std::uint64_t bytes)
{
    const rg::BufferHandle dst = builder.Import(host);
    std::optional<rg::PassBody> body = rg::MakeTransferBody(builder, name, rg::TransferRecord::Readback(src, dst, bytes));
    builder.KeepBufferOutput(dst);
    if (body.has_value()) {
        builder.AddRenderPass(name, rg::DefaultPassKind(rg::DeclarativePassType::Transfer), kGame,
            rg::RenderPassCategory::General, body->setup, body->execute,
            rg::DefaultDrawKind(rg::DeclarativePassType::Transfer), rg::RenderPassEvent::Opaques);
    }
}

TEST(FxBufferGpuTest, KitVerbsFillBindAndOrderOnARealDevice)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    constexpr std::uint32_t kCount = 100;
    constexpr std::uint64_t kBytes = kCount * sizeof(std::uint32_t);
    const rg::DeviceRef device = fixture.GetRenderer().Device();
    rg::GpuBuffer resultHost(device, kBytes, "Test.ResultHost", BufferUse::HostRead | BufferUse::TransferDst);
    rg::GpuBuffer counterHost(device, sizeof(std::uint32_t), "Test.CounterHost", BufferUse::HostRead | BufferUse::TransferDst);
    PersistentBuffer items("Test.Items", BufferUse::Storage | BufferUse::HostWrite);
    PersistentBuffer results("Test.Results", BufferUse::Storage | BufferUse::TransferSrc);
    PersistentBuffer counter("Test.Counter", BufferUse::Storage | BufferUse::TransferSrc);
    ComputeShader shader("BufferFixture");
    std::vector<std::uint32_t> values(kCount);
    std::iota(values.begin(), values.end(), 0u);

    bool dispatched = false;
    fixture.RunSynchronousFrame([&](rg::RenderGraphBuilder& builder) -> std::vector<rg::TextureHandle> {
        detail::FrameContext context;
        context.builder = &builder;
        context.scope = kGame;
        context.event = rg::RenderPassEvent::PreOpaques;
        context.serial = FrameSerial(0, kGame);
        PreOpaqueFrame f(View{}, FakeScene(), context.serial, &context);
        if (!f.ShaderReady(shader)) {
            return {};
        }
        const GpuBufferRef in = items.Acquire(f, kBytes);
        const GpuBufferRef out = results.Acquire(f, kBytes);
        const GpuBufferRef cnt = counter.Acquire(f, sizeof(std::uint32_t));
        if (!in.IsValid() || !out.IsValid() || !cnt.IsValid() || !items.Upload(f, values.data(), kBytes)
            || !f.FillBuffer("Test.Clear", cnt, 5u)) {
            return {};
        }
        dispatched = f.Compute(shader, "Test.Count")
                         .BindBuffer("inputData", in)
                         .BindBuffer("outputData", out)
                         .BindBuffer("counter", cnt)
                         .Push(kCount)
                         .Dispatch(Extent2D{ kCount, 1 });
        AddHostCopy(builder, "Test.CopyResults", out.handle, resultHost, kBytes);
        AddHostCopy(builder, "Test.CopyCounter", cnt.handle, counterHost, sizeof(std::uint32_t));
        return {};
    });
    ASSERT_TRUE(dispatched);

    std::vector<std::uint32_t> result(kCount);
    ASSERT_TRUE(resultHost.Download(result.data(), kBytes));
    for (std::uint32_t i = 0; i < kCount; ++i) {
        ASSERT_EQ(result[i], i * 2u) << "element " << i;
    }
    std::uint32_t total = 0;
    ASSERT_TRUE(counterHost.Download(&total, sizeof(total)));
    EXPECT_EQ(total, 5u + kCount); // The clear ran before the atomics and was visible to them.
}

} // namespace
} // namespace gte::fx
