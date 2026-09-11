#include "AtmosphereAerialPerspectiveSkyPurityValidation.h"

#include "../Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

namespace gte {

namespace {

// LOCAL copy, deliberately NOT shared with src/Encoding/DepthVisualization.h
// (see PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md, Step 2.4, for why) - decodes
// one already-captured depth texel's raw 4 bytes into its real [0,1] float
// value, for the exact three depth formats VulkanDevice::PickDepthFormat()
// can ever return.
bool DecodeDepthTexelToUnitFloat(std::uint32_t word, VkFormat depthFormat, float& outDepth) noexcept
{
    if (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        const std::uint32_t depth24 = word & 0x00FFFFFFu;
        outDepth = static_cast<float>(depth24) / static_cast<float>(0x00FFFFFFu);
        return true;
    }
    if (depthFormat == VK_FORMAT_D32_SFLOAT || depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        std::memcpy(&outDepth, &word, 4);
        return true;
    }
    return false; // Unrecognized format - caller reports a clean failure, never a crash.
}

// Returns true when `format`'s physical byte layout stores Blue before Red
// (e.g. VK_FORMAT_B8G8R8A8_UNORM/_SRGB - this engine's swapchain-negotiated
// color format, see VulkanSwapchain.cpp's ChooseSurfaceFormat()), false for a
// plain R8G8B8A8 layout. NEEDED here because "GameView"/"SceneView" (the
// PRE-composite target, created at Renderer::ColorFormat() - i.e. whatever
// the swapchain actually negotiated, commonly BGRA) and
// "GameViewComposited"/"SceneViewComposited" (the POST-composite target, a
// compute-shader storage image, always explicitly VK_FORMAT_R8G8B8A8_UNORM -
// see AtmosphereLutRenderer.cpp's EnsureAerialPerspectiveCompositeViewInitialized())
// can legitimately have DIFFERENT raw channel byte orders despite carrying
// the exact same LOGICAL color - comparing raw bytes channel-by-channel
// without accounting for this would report a large, systematic, false
// mismatch on every non-grayscale pixel (R and B swapped), exactly the same
// "caller decides whether a BGRA->RGBA swizzle is needed based on the real
// captured format" rule src/Encoding/PixelConversion.h's own
// ConvertBgraToRgbaInPlace() already documents for this exact same engine
// quirk.
bool IsBgraColorFormat(VkFormat format) noexcept
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

// Reads texel `pixel` (4 raw bytes) as its LOGICAL (R, G, B) triplet,
// accounting for `format`'s own physical channel order (see
// IsBgraColorFormat() above) - a BGRA-ordered texel's physical byte 0 is
// Blue and byte 2 is Red, so this swaps them back to logical order; an
// RGBA-ordered texel (or any other format - defensively treated as RGBA, the
// common case) is read as-is.
void ReadLogicalRgb(const std::uint8_t* pixel, VkFormat format, std::uint8_t outRgb[3]) noexcept
{
    if (IsBgraColorFormat(format)) {
        outRgb[0] = pixel[2]; // R physically stored at byte index 2.
        outRgb[1] = pixel[1];
        outRgb[2] = pixel[0]; // B physically stored at byte index 0.
    } else {
        outRgb[0] = pixel[0];
        outRgb[1] = pixel[1];
        outRgb[2] = pixel[2];
    }
}

} // namespace

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveSkyPurityResult& result)
{
    if (!result.succeeded) {
        return "Aerial Perspective Sky Purity Validation: FAILED - " + result.failureReason;
    }
    if (result.skyPixelCount == 0) {
        return "Aerial Perspective Sky Purity Validation: no sky pixels found this frame "
               "(scene is fully covered by opaque geometry) - nothing to check.";
    }

    char buffer[640];
    std::snprintf(buffer, sizeof(buffer),
        "Aerial Perspective Sky Purity Validation: %dx%d, %zu sky pixel(s) checked\n"
        "  mismatching sky pixels: %zu / %zu\n"
        "  max channel delta:      %.6f\n"
        "  mean channel delta:     %.6f\n"
        "  tolerance:              %.6f",
        result.width, result.height, result.skyPixelCount, result.mismatchingSkyPixelCount, result.skyPixelCount,
        result.maxSkyPixelChannelDelta, result.meanSkyPixelChannelDelta, result.toleranceUnorm8);
    return std::string(buffer);
}

AtmosphereAerialPerspectiveSkyPurityResult ValidateAerialPerspectiveSkyPurity(Renderer& renderer,
    const rg::RenderGraph& renderGraph, const char* preCompositeColorTextureName,
    const char* compositedColorTextureName, double toleranceUnorm8)
{
    AtmosphereAerialPerspectiveSkyPurityResult result;
    result.toleranceUnorm8 = toleranceUnorm8;

    const std::optional<rg::DebugTextureSnapshot> preSnapshot =
        renderGraph.DebugTextureSnapshotFor(preCompositeColorTextureName);
    if (!preSnapshot.has_value()) {
        result.failureReason =
            std::string("\"") + preCompositeColorTextureName + "\" is not currently registered - render at least one frame first.";
        return result;
    }
    if (!preSnapshot->hasDepth) {
        result.failureReason = std::string("\"") + preCompositeColorTextureName + "\" has no depth half registered.";
        return result;
    }

    const std::optional<rg::DebugTextureSnapshot> postSnapshot =
        renderGraph.DebugTextureSnapshotFor(compositedColorTextureName);
    if (!postSnapshot.has_value()) {
        result.failureReason =
            std::string("\"") + compositedColorTextureName + "\" is not currently registered - render at least one frame first.";
        return result;
    }

    if (preSnapshot->target.extent.width != postSnapshot->target.extent.width
        || preSnapshot->target.extent.height != postSnapshot->target.extent.height) {
        result.failureReason = "Pre-composite and post-composite textures have different extents this frame.";
        return result;
    }

    result.width = static_cast<int>(preSnapshot->target.extent.width);
    result.height = static_cast<int>(preSnapshot->target.extent.height);
    if (result.width <= 0 || result.height <= 0) {
        result.failureReason = "Textures have a zero-sized dimension - nothing to validate.";
        return result;
    }

    const Renderer::CapturedRawPixels depthRaw = renderer.CaptureImagePixels(preSnapshot->target.depthImage,
        VK_IMAGE_ASPECT_DEPTH_BIT, preSnapshot->target.depthFormat, preSnapshot->target.extent,
        preSnapshot->depthState, 4);
    const Renderer::CapturedRawPixels preColorRaw = renderer.CaptureImagePixels(preSnapshot->target.image,
        VK_IMAGE_ASPECT_COLOR_BIT, preSnapshot->target.format, preSnapshot->target.extent, preSnapshot->colorState,
        4);
    const Renderer::CapturedRawPixels postColorRaw = renderer.CaptureImagePixels(postSnapshot->target.image,
        VK_IMAGE_ASPECT_COLOR_BIT, postSnapshot->target.format, postSnapshot->target.extent,
        postSnapshot->colorState, 4);

    std::size_t skyPixelCount = 0;
    std::size_t mismatching = 0;
    double deltaSum = 0.0;
    double maxDelta = 0.0;

    const std::size_t pixelCount = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint32_t depthWord = 0;
        std::memcpy(&depthWord, depthRaw.pixels.data() + i * 4, 4);
        float depthValue = 1.0f;
        if (!DecodeDepthTexelToUnitFloat(depthWord, preSnapshot->target.depthFormat, depthValue)) {
            result.failureReason = "Unrecognized depth format - cannot decode.";
            return result;
        }

        if (!ShouldBypassAerialPerspectiveComposite(depthValue)) {
            continue; // Real opaque geometry - not this tool's concern (Phase 4's own regression tests cover that path).
        }
        ++skyPixelCount;

        const std::uint8_t* preTexel = preColorRaw.pixels.data() + i * 4;
        const std::uint8_t* postTexel = postColorRaw.pixels.data() + i * 4;

        // Read each texel's LOGICAL (R, G, B) triplet, independently
        // accounting for each texture's own possibly-different physical
        // channel byte order (see ReadLogicalRgb()'s own doc comment above -
        // "GameView" is commonly BGRA while "GameViewComposited" is always
        // explicitly RGBA) - comparing raw, un-swizzled bytes here would
        // report a large, systematic, false mismatch on every non-grayscale
        // sky pixel.
        std::uint8_t preRgb[3];
        std::uint8_t postRgb[3];
        ReadLogicalRgb(preTexel, preSnapshot->target.format, preRgb);
        ReadLogicalRgb(postTexel, postSnapshot->target.format, postRgb);

        double maxChannelDelta = 0.0;
        for (int channel = 0; channel < 3; ++channel) { // RGB only - alpha is not meaningful for either target.
            const double deltaUnit =
                std::abs(static_cast<double>(preRgb[channel]) - static_cast<double>(postRgb[channel])) / 255.0;
            maxChannelDelta = std::max(maxChannelDelta, deltaUnit);
        }

        deltaSum += maxChannelDelta;
        maxDelta = std::max(maxDelta, maxChannelDelta);
        if (maxChannelDelta > toleranceUnorm8) {
            ++mismatching;
        }
    }

    result.skyPixelCount = skyPixelCount;
    result.mismatchingSkyPixelCount = mismatching;
    result.maxSkyPixelChannelDelta = maxDelta;
    result.meanSkyPixelChannelDelta = (skyPixelCount > 0) ? (deltaSum / static_cast<double>(skyPixelCount)) : 0.0;
    result.succeeded = true;
    return result;
}

} // namespace gte
