// Tier-2 (headless GPU): a graph transfer pass copies a count into a host-readable buffer and the host reads it.

#include "Renderer/Buffer.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/TransferPass.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace gte::rg {
namespace {

TEST(TransferReadbackGpuTest, CountCopiedByTheGraphIsReadOnTheHost)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();

    constexpr std::uint32_t kCount = 1234;
    Buffer source = renderer.CreateStructuredBuffer(
        sizeof(std::uint32_t), 1, BufferMemoryUsage::CpuToGpu, BufferUsage::TransferSrc, "Test.Count");
    Buffer host = renderer.CreateStructuredBuffer(
        sizeof(std::uint32_t), 1, BufferMemoryUsage::GpuToCpu, BufferUsage::TransferDst, "Test.CountReadback");
    source.Upload(&kCount, sizeof(kCount));
    const std::uint32_t zero = 0;
    host.Upload(&zero, sizeof(zero));

    bool recorded = false;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& builder) -> std::vector<TextureHandle> {
        const BufferHandle src = builder.ImportBuffer("Test.Count", source.ObjectHandle(), sizeof(kCount));
        const BufferHandle dst = builder.ImportBuffer("Test.CountReadback", host.ObjectHandle(), sizeof(kCount));
        recorded = TransferPass(builder, "Test.CountReadback", ViewScope::Shared, RenderPassEvent::Opaques)
                       .Readback(src, dst, sizeof(kCount))
                       .Record();
        return {};
    });
    ASSERT_TRUE(recorded);

    std::uint32_t read = 0;
    ASSERT_TRUE(host.Download(&read, sizeof(read)));
    EXPECT_EQ(read, kCount);
}

} // namespace
} // namespace gte::rg
