// Tier-2 tests: graph buffers bound by name to a compute shader are ordered, barriered and kept alive.

#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/GpuBuffer.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RecordedPass.h"
#include "Renderer/RenderGraph/Shader.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace gte::rg {
namespace {

bool AddTransferPass(RenderGraphBuilder& builder, const char* name, const TransferRecord& record)
{
    std::optional<PassBody> body = MakeTransferBody(builder, name, record);
    if (!body.has_value()) {
        return false;
    }
    builder.AddRenderPass(name, DefaultPassKind(DeclarativePassType::Transfer), ViewScope::Primary,
        RenderPassCategory::General, body->setup, body->execute, DefaultDrawKind(DeclarativePassType::Transfer),
        RenderPassEvent::Opaques);
    return true;
}

TEST(TrackedBufferGpuTest, ComputeOverGraphBuffersIsOrderedAfterFillAndBeforeReadback)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    const DeviceRef device = fixture.GetRenderer().Device();
    constexpr std::uint32_t kCount = 200;
    constexpr std::size_t kBytes = kCount * sizeof(std::uint32_t);

    GpuBuffer input(device, kBytes, "Test.Input", GpuBufferUse::Storage | GpuBufferUse::HostWrite);
    GpuBuffer output(device, kBytes, "Test.Output", GpuBufferUse::Storage | GpuBufferUse::TransferSrc);
    GpuBuffer counter(device, sizeof(std::uint32_t), "Test.Counter",
        GpuBufferUse::Storage | GpuBufferUse::TransferSrc | GpuBufferUse::TransferDst);
    GpuBuffer outputHost(device, kBytes, "Test.OutputHost", GpuBufferUse::HostRead | GpuBufferUse::TransferDst);
    GpuBuffer counterHost(
        device, sizeof(std::uint32_t), "Test.CounterHost", GpuBufferUse::HostRead | GpuBufferUse::TransferDst);

    std::vector<std::uint32_t> values(kCount);
    for (std::uint32_t i = 0; i < kCount; ++i) {
        values[i] = i;
    }
    ASSERT_TRUE(input.Upload(values.data(), kBytes));

    Shader shader("BufferFixture", ShaderKind::Compute);
    bool dispatched = false;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& builder) -> std::vector<TextureHandle> {
        const BufferHandle in = builder.Import(input);
        const BufferHandle out = builder.Import(output);
        const BufferHandle cnt = builder.Import(counter);
        const BufferHandle outHost = builder.Import(outputHost);
        const BufferHandle cntHost = builder.Import(counterHost);

        AddTransferPass(builder, "Test.Fill", TransferRecord::Fill(cnt, 7u));
        dispatched = ComputePass(builder, shader, "Test.Compute", ViewScope::Primary, RenderPassEvent::Opaques)
                         .Bind("inputData", in)
                         .Bind("outputData", out)
                         .Bind("counter", cnt)
                         .Push(kCount)
                         .Dispatch(Extent2D{ kCount, 1 });
        AddTransferPass(builder, "Test.CopyOutput", TransferRecord::Readback(out, outHost, kBytes));
        AddTransferPass(builder, "Test.CopyCounter", TransferRecord::Readback(cnt, cntHost, sizeof(std::uint32_t)));
        builder.KeepBufferOutput(outHost);
        builder.KeepBufferOutput(cntHost);
        return {};
    });
    ASSERT_TRUE(dispatched);

    std::vector<std::uint32_t> result(kCount);
    ASSERT_TRUE(outputHost.Download(result.data(), kBytes));
    for (std::uint32_t i = 0; i < kCount; ++i) {
        ASSERT_EQ(result[i], i * 2u) << "element " << i;
    }
    std::uint32_t total = 0;
    ASSERT_TRUE(counterHost.Download(&total, sizeof(total)));
    EXPECT_EQ(total, 7u + kCount);
}

TEST(TrackedBufferGpuTest, TheLastOfTwoConsecutiveFillsWins)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    const DeviceRef device = fixture.GetRenderer().Device();
    GpuBuffer target(device, sizeof(std::uint32_t), "Test.Target", GpuBufferUse::TransferSrc | GpuBufferUse::TransferDst);
    GpuBuffer host(device, sizeof(std::uint32_t), "Test.TargetHost", GpuBufferUse::HostRead | GpuBufferUse::TransferDst);

    fixture.RunSynchronousFrame([&](RenderGraphBuilder& builder) -> std::vector<TextureHandle> {
        const BufferHandle dst = builder.Import(target);
        const BufferHandle hostHandle = builder.Import(host);
        AddTransferPass(builder, "Test.FillA", TransferRecord::Fill(dst, 1u));
        AddTransferPass(builder, "Test.FillB", TransferRecord::Fill(dst, 2u));
        AddTransferPass(builder, "Test.Copy", TransferRecord::Readback(dst, hostHandle, sizeof(std::uint32_t)));
        builder.KeepBufferOutput(hostHandle);
        return {};
    });

    std::uint32_t value = 0;
    ASSERT_TRUE(host.Download(&value, sizeof(value)));
    EXPECT_EQ(value, 2u);
}

TEST(TrackedBufferGpuTest, ADispatchOverThePerAxisGroupLimitIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    test::RecordingLogSink sink;
    const test::ScopedLogSinkInstall guard(&sink);
    const DeviceRef device = fixture.GetRenderer().Device();
    GpuBuffer input(device, 256, "Test.Input", GpuBufferUse::Storage);
    GpuBuffer output(device, 256, "Test.Output", GpuBufferUse::Storage);
    GpuBuffer counter(device, 256, "Test.Counter", GpuBufferUse::Storage);

    Shader shader("BufferFixture", ShaderKind::Compute);
    bool refused = true;
    bool accepted = false;
    std::size_t passesAfterRefusal = 0;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& builder) -> std::vector<TextureHandle> {
        const BufferHandle in = builder.Import(input);
        const BufferHandle out = builder.Import(output);
        const BufferHandle cnt = builder.Import(counter);
        constexpr std::uint32_t kLocal = 64;
        refused = ComputePass(builder, shader, "Test.TooBig", ViewScope::Primary, RenderPassEvent::Opaques)
                      .Bind("inputData", in)
                      .Bind("outputData", out)
                      .Bind("counter", cnt)
                      .Push(1u)
                      .Dispatch(Extent2D{ kLocal * 65535u + 1u, 1 });
        passesAfterRefusal = builder.DeclaredPassCount();
        accepted = ComputePass(builder, shader, "Test.AtLimit", ViewScope::Primary, RenderPassEvent::Opaques)
                       .Bind("inputData", in)
                       .Bind("outputData", out)
                       .Bind("counter", cnt)
                       .Push(1u)
                       .Dispatch(Extent2D{ kLocal * 65535u, 1 });
        return {};
    });

    EXPECT_FALSE(refused);
    EXPECT_EQ(passesAfterRefusal, 0u);
    EXPECT_TRUE(accepted);
    EXPECT_EQ(test::CountErrorsByCategory(sink, "PassChain"), 1);
}

} // namespace
} // namespace gte::rg
