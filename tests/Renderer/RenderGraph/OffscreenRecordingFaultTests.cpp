// Tier-2 (headless GPU): a throw while recording the offscreen frame must not poison later frames.

#include "Renderer/Buffer.h"
#include "Renderer/OffscreenRecording.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RecordedPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/TransferPass.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace gte::rg {
namespace {

using Clock = std::chrono::steady_clock;

TEST(OffscreenRecordingFaultTest, ThrowDuringRecordIsDiscardedAndLaterFramesRun)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();

    constexpr std::uint32_t kCount = 4321;
    Buffer source = renderer.CreateBuffer(
        sizeof(std::uint32_t), BufferUsage::StorageBuffer | BufferUsage::TransferSrc, BufferMemoryUsage::CpuToGpu, "Test.Count");
    Buffer host = renderer.CreateBuffer(sizeof(std::uint32_t), BufferUsage::StorageBuffer | BufferUsage::TransferDst,
        BufferMemoryUsage::GpuToCpu, "Test.CountReadback");
    source.Upload(&kCount, sizeof(kCount));
    const std::uint32_t zero = 0;
    host.Upload(&zero, sizeof(zero));

    EXPECT_THROW(fixture.RunSynchronousFrame([](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        throw std::runtime_error("injected record failure");
    }),
        std::runtime_error);

    constexpr auto kFrameBudget = std::chrono::seconds(2);
    for (int frame = 0; frame < 2; ++frame) {
        const Clock::time_point start = Clock::now();
        fixture.RunSynchronousFrame([](RenderGraphBuilder&) -> std::vector<TextureHandle> { return {}; });
        EXPECT_LT(Clock::now() - start, kFrameBudget);
    }

    const Clock::time_point start = Clock::now();
    bool recorded = false;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& builder) -> std::vector<TextureHandle> {
        const BufferHandle src = builder.ImportBuffer("Test.Count", source.ObjectHandle(), sizeof(kCount));
        const BufferHandle dst = builder.ImportBuffer("Test.CountReadback", host.ObjectHandle(), sizeof(kCount));
        std::optional<PassBody> body =
            MakeTransferBody(builder, "Test.CountReadback", TransferRecord::Readback(src, dst, sizeof(kCount)));
        if (body.has_value()) {
            builder.AddRenderPass("Test.CountReadback", DefaultPassKind(DeclarativePassType::Transfer), ViewScope::Shared,
                RenderPassCategory::General, body->setup, body->execute, DefaultDrawKind(DeclarativePassType::Transfer),
                RenderPassEvent::Opaques);
            builder.KeepBufferOutput(dst);
            recorded = true;
        }
        return {};
    });
    EXPECT_LT(Clock::now() - start, kFrameBudget);
    ASSERT_TRUE(recorded);

    std::uint32_t read = 0;
    ASSERT_TRUE(host.Download(&read, sizeof(read)));
    EXPECT_EQ(read, kCount);
}

TEST(OffscreenRecordingFaultTest, SecondOpenRecordingThrowsAndFirstStaysValid)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();

    {
        OffscreenRecording first = renderer.BeginOffscreenRecording();
        EXPECT_THROW(static_cast<void>(renderer.BeginOffscreenRecording()), std::logic_error);
        EXPECT_TRUE(static_cast<bool>(first.Context()));
        first.Submit();
        EXPECT_THROW(first.Submit(), std::logic_error);
    }

    // A discarded recording leaves the slot free again.
    {
        OffscreenRecording dropped = renderer.BeginOffscreenRecording();
    }
    OffscreenRecording again = renderer.BeginOffscreenRecording();
    again.Submit();
}

} // namespace
} // namespace gte::rg
