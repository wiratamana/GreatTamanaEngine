// Tests for rg::GpuBuffer: use mapping and range checks (no GPU), and a graph copy into a host-read buffer (headless GPU).

#include "Renderer/RenderGraph/GpuBuffer.h"
#include "Renderer/RenderGraph/GpuBufferInternal.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RecordedPass.h"
#include "Renderer/RenderGraph/RenderGraph.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/TransferPass.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace gte::rg {
namespace {

TEST(GpuBufferPlanTest, StorageOnlyIsDeviceLocalAndAlwaysATransferDestination)
{
    const GpuBufferPlan plan = PlanGpuBuffer(GpuBufferUse::Storage);
    ASSERT_TRUE(plan.Ok()) << plan.error;
    EXPECT_EQ(plan.memory, BufferMemoryUsage::GpuOnly);
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::StorageBuffer));
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::TransferDst));
}

TEST(GpuBufferPlanTest, HostWriteMapsToCpuToGpuAndHostReadToGpuToCpu)
{
    const GpuBufferPlan write = PlanGpuBuffer(GpuBufferUse::Storage | GpuBufferUse::HostWrite);
    ASSERT_TRUE(write.Ok());
    EXPECT_EQ(write.memory, BufferMemoryUsage::CpuToGpu);

    const GpuBufferPlan read = PlanGpuBuffer(GpuBufferUse::TransferDst | GpuBufferUse::HostRead);
    ASSERT_TRUE(read.Ok());
    EXPECT_EQ(read.memory, BufferMemoryUsage::GpuToCpu);
    EXPECT_TRUE(HasUsage(read.usage, BufferUsage::TransferDst));
}

TEST(GpuBufferPlanTest, EveryUseBitMapsToItsOwnEngineUsage)
{
    const GpuBufferPlan plan = PlanGpuBuffer(GpuBufferUse::Storage | GpuBufferUse::Uniform | GpuBufferUse::Indirect
        | GpuBufferUse::Vertex | GpuBufferUse::TransferSrc);
    ASSERT_TRUE(plan.Ok());
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::StorageBuffer));
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::UniformBuffer));
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::IndirectBuffer));
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::VertexBuffer));
    EXPECT_TRUE(HasUsage(plan.usage, BufferUsage::TransferSrc));
}

TEST(GpuBufferPlanTest, HostReadPlusHostWriteAndUselessBuffersAreRefused)
{
    EXPECT_FALSE(PlanGpuBuffer(GpuBufferUse::Storage | GpuBufferUse::HostRead | GpuBufferUse::HostWrite).Ok());
    EXPECT_FALSE(PlanGpuBuffer(GpuBufferUse::None).Ok());
    EXPECT_FALSE(PlanGpuBuffer(GpuBufferUse::HostWrite).Ok());
}

TEST(GpuBufferRangeTest, FitsInsideTheCapacityAndNeverOverflows)
{
    EXPECT_TRUE(GpuBufferRangeFits(16, 0, 16));
    EXPECT_TRUE(GpuBufferRangeFits(16, 12, 4));
    EXPECT_TRUE(GpuBufferRangeFits(16, 16, 0));
    EXPECT_FALSE(GpuBufferRangeFits(16, 12, 5));
    EXPECT_FALSE(GpuBufferRangeFits(16, 17, 0));
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(GpuBufferRangeFits(16, 8, kMax));
    EXPECT_FALSE(GpuBufferRangeFits(16, kMax, 8));
}

TEST(GpuBufferTest, AnEmptyBufferIsInvalidAndRefusesEveryOperation)
{
    GpuBuffer buffer;
    const std::uint32_t value = 1;
    std::uint32_t out = 0;
    EXPECT_FALSE(buffer.IsValid());
    EXPECT_EQ(buffer.Size(), 0u);
    EXPECT_EQ(buffer.Handle(), BufferObjectHandle::Invalid);
    EXPECT_FALSE(buffer.Upload(&value, sizeof(value)));
    EXPECT_FALSE(buffer.Download(&out, sizeof(out)));
}

TEST(GpuBufferTest, ADefaultDeviceRefCannotCreateABuffer)
{
    EXPECT_FALSE(DeviceRef().IsValid());
    EXPECT_THROW(GpuBuffer(DeviceRef(), 16, "Test.NoDevice", GpuBufferUse::Storage), std::runtime_error);
}

TEST(GpuBufferGpuTest, UploadedValuesAreCopiedByTheGraphIntoAHostReadBufferAndDownloaded)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    const DeviceRef device = fixture.GetRenderer().Device();

    constexpr std::uint32_t kValues[4] = { 7, 11, 13, 17 };
    GpuBuffer source(device, sizeof(kValues), "Test.Source", GpuBufferUse::Storage | GpuBufferUse::HostWrite | GpuBufferUse::TransferSrc);
    GpuBuffer host(device, sizeof(kValues), "Test.Readback", GpuBufferUse::HostRead | GpuBufferUse::TransferDst);
    ASSERT_TRUE(source.Upload(kValues, sizeof(kValues)));
    EXPECT_FALSE(source.Upload(kValues, sizeof(kValues), 4)) << "a range past the end must be refused";

    bool recorded = false;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& builder) -> std::vector<TextureHandle> {
        const BufferHandle src = builder.Import(source);
        const BufferHandle dst = builder.Import(host);
        std::optional<PassBody> body =
            MakeTransferBody(builder, "Test.Readback", TransferRecord::Readback(src, dst, sizeof(kValues)));
        if (body.has_value()) {
            builder.AddRenderPass("Test.Readback", DefaultPassKind(DeclarativePassType::Transfer), ViewScope::Shared,
                RenderPassCategory::General, body->setup, body->execute, DefaultDrawKind(DeclarativePassType::Transfer),
                RenderPassEvent::Opaques);
            builder.KeepBufferOutput(dst);
            recorded = true;
        }
        return {};
    });
    ASSERT_TRUE(recorded);

    std::uint32_t read[4] = {};
    ASSERT_TRUE(host.Download(read, sizeof(read)));
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(read[i], kValues[i]);
    }
    EXPECT_FALSE(host.Upload(kValues, sizeof(kValues))) << "a HostRead buffer is not writable";
}

} // namespace
} // namespace gte::rg
