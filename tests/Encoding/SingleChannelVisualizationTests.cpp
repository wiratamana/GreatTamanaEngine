// Tier-2 (real, headless GPU, tests/Fakes/HeadlessRenderGraphFixture.h)
// round-trip test for Convert1ChannelToGrayscaleRgba8() (src/Encoding/
// SingleChannelVisualization.h): a real VK_FORMAT_R8_UNORM RenderTexture is
// cleared to a known value via a real render graph pass, captured back via
// Renderer::CaptureImagePixels(), converted to grayscale RGBA8, encoded to a
// real PNG, then decoded via stb_image.h (already vendored - see
// PngEncoderTests.cpp's own identical precedent) and compared pixel-for-pixel.

#include "Encoding/SingleChannelVisualization.h"
#include "Encoding/PngEncoder.h"
#include "Renderer/RenderTexture.h"
#include "Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>
#include <stb_image.h>

#include <array>
#include <cstdint>
#include <vector>

namespace gte::Encoding {
namespace {

TEST(SingleChannelVisualizationTest, RealR8UnormCaptureRoundTripsThroughPng)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }

    constexpr int kWidth = 8;
    constexpr int kHeight = 8;
    constexpr std::array<float, 4> kKnownClearColor{ 128.0f / 255.0f, 0.0f, 0.0f, 0.0f };

    RenderTexture tex = fixture.GetRenderer().CreateRenderTexture(
        kWidth, kHeight, VK_FORMAT_R8_UNORM, "SingleChannelVisualizationTarget", /*depthDebugName=*/nullptr,
        /*allowStorageImageAccess=*/false, /*allowDepthSampledAccess=*/false,
        /*createDepthCompanion=*/false, /*createColorImage=*/true);

    fixture.RunSynchronousFrame([&](rg::RenderGraphBuilder& b) -> std::vector<rg::TextureHandle> {
        const rg::TextureHandle h =
            b.ImportTexture("SingleChannelVisualizationTarget", tex.Target(), VK_IMAGE_LAYOUT_UNDEFINED);
        b.AddPass(
            "SingleChannelVisualizationKnownPatternWrite",
            [&](rg::RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h, kKnownClearColor); },
            [](rg::PassContext&) {});
        return { h }; // Must be a root - a write-only pass with no reader is culled otherwise.
    });

    const std::optional<rg::DebugTextureSnapshot> snapshot =
        fixture.GetRenderGraph().DebugTextureSnapshotFor("SingleChannelVisualizationTarget");
    ASSERT_TRUE(snapshot.has_value());

    const Renderer::CapturedRawPixels raw = fixture.GetRenderer().CaptureImagePixels(
        tex.Image(), VK_IMAGE_ASPECT_COLOR_BIT, tex.Format(), tex.Extent(), snapshot->colorState);
    ASSERT_FALSE(raw.pixels.empty());
    ASSERT_EQ(raw.pixels.size(), static_cast<std::size_t>(kWidth) * kHeight);

    std::vector<std::uint8_t> converted(static_cast<std::size_t>(kWidth) * kHeight * 4);
    ASSERT_TRUE(Convert1ChannelToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, converted.data()));

    const std::vector<std::uint8_t> png = EncodeRgba8ToPng(converted.data(), raw.width, raw.height);
    ASSERT_FALSE(png.empty());

    int decodedWidth = 0;
    int decodedHeight = 0;
    int decodedChannels = 0;
    stbi_uc* decoded = stbi_load_from_memory(
        png.data(), static_cast<int>(png.size()), &decodedWidth, &decodedHeight, &decodedChannels, 4);
    ASSERT_NE(decoded, nullptr);
    EXPECT_EQ(decodedWidth, kWidth);
    EXPECT_EQ(decodedHeight, kHeight);

    // The exact value the GPU actually wrote for the known clear - read back
    // from the real capture, never re-derived from kKnownClearColor's own
    // float value (avoids a float->uint8 rounding mismatch with the driver's
    // own conversion).
    const std::uint8_t expectedValue = raw.pixels[0];
    for (int i = 0; i < kWidth * kHeight; ++i) {
        EXPECT_EQ(decoded[i * 4 + 0], expectedValue);
        EXPECT_EQ(decoded[i * 4 + 1], expectedValue);
        EXPECT_EQ(decoded[i * 4 + 2], expectedValue);
        EXPECT_EQ(decoded[i * 4 + 3], 255);
    }
    stbi_image_free(decoded);
}

} // namespace
} // namespace gte::Encoding
