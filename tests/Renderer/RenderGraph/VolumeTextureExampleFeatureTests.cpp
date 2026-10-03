// Real, headless-GPU, numerically-asserted tests proving both valid
// compute-dispatch shapes for filling a VolumeTexture - per-cell-parallel
// (every cell computed independently) and per-column-sequential (every
// column loops its own Z slices in order, since each slice's value depends
// on the running state accumulated by every slice before it). See
// docs/conventions/volumetric-resources.md for the full recipe these two
// tests exist to prove. Built on the same real HeadlessRenderGraphFixture
// every other Tier-2 test in this folder already uses.

#include "Renderer/Renderer.h"
#include "Renderer/ComputeDescriptorSet.h"
#include "Renderer/ComputePipeline.h"
#include "Renderer/VolumetricFroxelMath.h"
#include "Encoding/HdrColorVisualization.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace gte::rg {
namespace {

constexpr float kEpsilon = 0.01f;

// One decoded VK_FORMAT_R16G16B16A16_SFLOAT texel (8 bytes, 4 half-floats,
// R-G-B-A order).
struct DecodedTexel {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

DecodedTexel DecodeRgba16fTexel(const std::vector<std::uint8_t>& pixels, std::size_t texelIndex)
{
    std::uint16_t channels[4] = { 0, 0, 0, 0 };
    std::memcpy(channels, pixels.data() + texelIndex * 8, 8);
    return DecodedTexel{ Encoding::DecodeHalfFloat(channels[0]), Encoding::DecodeHalfFloat(channels[1]),
        Encoding::DecodeHalfFloat(channels[2]), Encoding::DecodeHalfFloat(channels[3]) };
}

} // namespace

// Per-cell-parallel: every (x, y, z) cell encodes its own coordinates with no
// cross-invocation dependency at all, so dispatch order can never matter for
// a correctly-independent fill. Reads back every one of the 4x4x4 = 64 cells
// and confirms each landed at exactly the coordinate it was told to encode.
TEST(VolumeTextureExampleFeatureTest, PerCellParallelFillWritesEveryCellsOwnCoordinates)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    constexpr std::uint32_t kSize = 4;
    constexpr const char* kVolumeName = "PerCellFillTestVolume";

    VolumeTexture volume = fixture.GetRenderer().CreateVolumeTexture(
        static_cast<int>(kSize), static_cast<int>(kSize), static_cast<int>(kSize), VK_FORMAT_R16G16B16A16_SFLOAT, kVolumeName);

    ComputePipeline pipeline = fixture.GetRenderer().CreateComputePipeline(GTE_TEST_FROXEL_PERCELL_SPV_PATH);
    const VkDescriptorSet rawSet =
        fixture.GetRenderer().AllocateComputeDescriptorSet(pipeline.ReflectedDescriptorSetLayout(0));
    ComputeDescriptorSet descriptorSet(rawSet);
    const VkDevice device = fixture.GetRenderer().GetVulkanContextInfo().device;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const VolumeTextureHandle handle = b.ImportVolumeTexture(kVolumeName, volume.Target(), VK_IMAGE_LAYOUT_UNDEFINED);
        b.AddRenderPass(
            "VolumeTextureExamplePerCellFillPass", PassKind::Compute,
            [handle](RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteVolumeTexture(handle, ResourceAccess::ComputeShaderWrite);
            },
            [&](PassContext& ctx) {
                const PassContext::ResolvedVolumeTexture dest = ctx.resolveVolumeTexture(handle);
                descriptorSet.Rewrite(device, { ComputeDescriptorWrite::StorageImage(0, dest.view) });

                auto cmd = ctx.Cmd();
                cmd.BindComputePipeline(pipeline);
                cmd.BindDescriptorSet(descriptorSet.Native());
                cmd.DispatchOverSize(kSize, kSize, kSize);
            });
        // Mandatory - a pass whose only write is a VolumeTextureHandle is
        // silently culled without this call.
        b.KeepVolumeTextureOutput(handle);
        return {};
    });

    const std::optional<DebugVolumeTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugVolumeTextureSnapshotFor(kVolumeName);
    ASSERT_TRUE(snapshot.has_value());

    for (std::uint32_t z = 0; z < kSize; ++z) {
        const Renderer::CapturedRawPixels captured = fixture.GetRenderer().CaptureImagePixels(snapshot->target.image,
            VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format, VkExtent2D{ kSize, kSize }, snapshot->state,
            /*bytesPerPixel=*/8, /*zOffset=*/z, /*depth=*/1);
        ASSERT_EQ(captured.pixels.size(), static_cast<std::size_t>(kSize) * kSize * 8);

        for (std::uint32_t y = 0; y < kSize; ++y) {
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const std::size_t texelIndex = static_cast<std::size_t>(y) * kSize + x;
                const DecodedTexel texel = DecodeRgba16fTexel(captured.pixels, texelIndex);
                EXPECT_NEAR(texel.r, static_cast<float>(x), kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(texel.g, static_cast<float>(y), kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(texel.b, static_cast<float>(z), kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(texel.a, 1.0f, kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
            }
        }
    }
}

// Per-column-sequential: every (x, y) column loops its own 8 Z slices
// internally, accumulating a running total via the SAME
// VolumetricFroxelMath.glsl/.h FroxelSliceToViewDepth() formula the shader
// #includes - proving (a) the sequential dispatch shape really executes in
// order, (b) the shared GLSL file is genuinely #include-able/callable by a
// second, independent shader, and (c) the GLSL and C++ oracles agree exactly.
TEST(VolumeTextureExampleFeatureTest, PerColumnSequentialFillMatchesTheCpuOracleRunningTotal)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    constexpr std::uint32_t kWidth = 4;
    constexpr std::uint32_t kHeight = 4;
    constexpr std::uint32_t kDepth = 8;
    constexpr float kMaxDistanceKm = 10.0f;
    constexpr float kDepthExponent = 2.0f;
    constexpr const char* kVolumeName = "PerColumnFillTestVolume";

    VolumeTexture volume = fixture.GetRenderer().CreateVolumeTexture(
        static_cast<int>(kWidth), static_cast<int>(kHeight), static_cast<int>(kDepth), VK_FORMAT_R16G16B16A16_SFLOAT, kVolumeName);

    ComputePipeline pipeline = fixture.GetRenderer().CreateComputePipeline(GTE_TEST_FROXEL_PERCOLUMN_SPV_PATH);
    const VkDescriptorSet rawSet =
        fixture.GetRenderer().AllocateComputeDescriptorSet(pipeline.ReflectedDescriptorSetLayout(0));
    ComputeDescriptorSet descriptorSet(rawSet);
    const VkDevice device = fixture.GetRenderer().GetVulkanContextInfo().device;

    struct PushConstants {
        float maxDistanceKm;
        float depthExponent;
    };
    const PushConstants pushConstants{ kMaxDistanceKm, kDepthExponent };

    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const VolumeTextureHandle handle = b.ImportVolumeTexture(kVolumeName, volume.Target(), VK_IMAGE_LAYOUT_UNDEFINED);
        b.AddRenderPass(
            "VolumeTextureExamplePerColumnFillPass", PassKind::Compute,
            [handle](RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteVolumeTexture(handle, ResourceAccess::ComputeShaderWrite);
            },
            [&](PassContext& ctx) {
                const PassContext::ResolvedVolumeTexture dest = ctx.resolveVolumeTexture(handle);
                descriptorSet.Rewrite(device, { ComputeDescriptorWrite::StorageImage(0, dest.view) });

                auto cmd = ctx.Cmd();
                cmd.BindComputePipeline(pipeline);
                cmd.BindDescriptorSet(descriptorSet.Native());
                cmd.SetPushConstants(pushConstants);
                cmd.DispatchOverSize(kWidth, kHeight, 1);
            });
        // Mandatory - same reasoning as the per-cell test above.
        b.KeepVolumeTextureOutput(handle);
        return {};
    });

    const std::optional<DebugVolumeTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugVolumeTextureSnapshotFor(kVolumeName);
    ASSERT_TRUE(snapshot.has_value());

    // Independently compute, in C++, the SAME running total the shader
    // accumulates - if the GLSL and C++ oracles ever disagree, this test
    // fails here, never silently.
    std::vector<float> expectedRunningTotalPerSlice(kDepth, 0.0f);
    float runningTotal = 0.0f;
    for (std::uint32_t slice = 0; slice < kDepth; ++slice) {
        const float viewDepthKm =
            FroxelSliceToViewDepth(static_cast<float>(slice), static_cast<float>(kDepth), kMaxDistanceKm, kDepthExponent);
        runningTotal += viewDepthKm;
        expectedRunningTotalPerSlice[slice] = runningTotal;
    }

    for (std::uint32_t z = 0; z < kDepth; ++z) {
        const Renderer::CapturedRawPixels captured = fixture.GetRenderer().CaptureImagePixels(snapshot->target.image,
            VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format, VkExtent2D{ kWidth, kHeight }, snapshot->state,
            /*bytesPerPixel=*/8, /*zOffset=*/z, /*depth=*/1);
        ASSERT_EQ(captured.pixels.size(), static_cast<std::size_t>(kWidth) * kHeight * 8);

        const float expected = expectedRunningTotalPerSlice[z];
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::size_t texelIndex = static_cast<std::size_t>(y) * kWidth + x;
                const DecodedTexel texel = DecodeRgba16fTexel(captured.pixels, texelIndex);
                EXPECT_NEAR(texel.r, expected, kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(texel.g, expected, kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(texel.b, expected, kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
                EXPECT_NEAR(texel.a, 1.0f, kEpsilon) << "x=" << x << " y=" << y << " z=" << z;
            }
        }
    }
}

} // namespace gte::rg
