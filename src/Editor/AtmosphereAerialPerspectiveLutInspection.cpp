#include "AtmosphereAerialPerspectiveLutInspection.h"

#include "../Encoding/HdrColorVisualization.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gte {

namespace {

// A documented, tunable HEURISTIC threshold - see
// AtmosphereAerialPerspectiveLutInspectionResult::likelyVisibleAtDefaultExposure's
// own doc comment (AtmosphereAerialPerspectiveLutInspection.h) for the full
// reasoning: an 8-bit channel's own smallest visible step is 1/255 ~= 0.0039;
// this heuristic requires at least 5x that much combined haze/in-scattering
// "budget" before calling the volume's contents plausibly visible.
constexpr double kQuantizationStep = 1.0 / 255.0;
constexpr double kLikelyVisibleThreshold = 5.0 * kQuantizationStep;

// Distinct from "AtmosphereAerialPerspectiveVolumeDebugSlice" (the separate,
// per-frame, Editor-slider-driven, GET /get_texture-capturable debug-slice
// texture Phase 9 of the original atmosphere-scattering-1 campaign already
// ships) - see AtmosphereLutRenderer.h's own
// CaptureAerialPerspectiveVolumeSliceImmediate() doc comment for why this
// MUST be a different literal.
constexpr const char* kInspectionOutputTextureName = "AtmosphereAerialPerspectiveVolumeInspectionSlice";

} // namespace

AerialPerspectiveSliceStats AccumulateAerialPerspectiveSliceStats(
    const std::uint8_t* rawRgba16fBytes, std::size_t texelCount, AerialPerspectiveSliceStats accumulateInto)
{
    AerialPerspectiveSliceStats stats = accumulateInto;
    if (rawRgba16fBytes == nullptr || texelCount == 0) {
        return stats;
    }

    for (std::size_t i = 0; i < texelCount; ++i) {
        std::uint16_t channels[4] = { 0, 0, 0, 0 };
        std::memcpy(channels, rawRgba16fBytes + i * 8, 8);

        // See this file's own header doc comment (and AtmosphereAerialPerspectiveLutInspection.h's
        // own top-of-file comment) for why this MUST go through
        // Encoding::DecodeHalfFloat() rather than a reinterpret_cast<const
        // float*> of the raw captured buffer - VK_FORMAT_R16G16B16A16_SFLOAT
        // is 4 IEEE-754 HALF floats/texel (2 bytes/channel), never 4 full
        // 32-bit floats.
        const float r = Encoding::DecodeHalfFloat(channels[0]);
        const float g = Encoding::DecodeHalfFloat(channels[1]);
        const float b = Encoding::DecodeHalfFloat(channels[2]);
        const float a = Encoding::DecodeHalfFloat(channels[3]); // transmittance

        stats.minTransmittance = std::min(stats.minTransmittance, a);
        stats.maxTransmittance = std::max(stats.maxTransmittance, a);
        stats.sumTransmittance += static_cast<double>(a);

        const float magnitude = std::sqrt(r * r + g * g + b * b);
        stats.minInScatteringMagnitude = std::min(stats.minInScatteringMagnitude, magnitude);
        stats.maxInScatteringMagnitude = std::max(stats.maxInScatteringMagnitude, magnitude);
        stats.sumInScatteringMagnitude += static_cast<double>(magnitude);

        stats.texelCount += 1;
    }

    return stats;
}

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveLutInspectionResult& result)
{
    if (!result.succeeded) {
        return "Aerial Perspective LUT Inspection: FAILED - " + result.failureReason;
    }

    char buffer[768];
    std::snprintf(buffer, sizeof(buffer),
        "Aerial Perspective LUT Inspection: %dx%dx%d (%zu texels)\n"
        "  transmittance   min=%.6f  max=%.6f  mean=%.6f\n"
        "  in-scattering   min=%.6f  max=%.6f  mean=%.6f\n"
        "  %s",
        result.width, result.height, result.depth, result.texelCount, result.minTransmittance,
        result.maxTransmittance, result.meanTransmittance, result.minInScatteringMagnitude,
        result.maxInScatteringMagnitude, result.meanInScatteringMagnitude,
        result.likelyVisibleAtDefaultExposure ? "LIKELY VISIBLE at default exposure"
                                               : "LIKELY TOO FAINT at default exposure");
    return std::string(buffer);
}

AtmosphereAerialPerspectiveLutInspectionResult FinalizeAerialPerspectiveLutInspection(
    const AerialPerspectiveSliceStats& totalStats, int width, int height, int depth)
{
    AtmosphereAerialPerspectiveLutInspectionResult result;
    result.succeeded = true;
    result.width = width;
    result.height = height;
    result.depth = depth;
    result.texelCount = totalStats.texelCount;

    result.minTransmittance = totalStats.minTransmittance;
    result.maxTransmittance = totalStats.maxTransmittance;
    result.meanTransmittance = totalStats.texelCount > 0
        ? static_cast<float>(totalStats.sumTransmittance / static_cast<double>(totalStats.texelCount))
        : 1.0f;

    result.minInScatteringMagnitude = totalStats.minInScatteringMagnitude;
    result.maxInScatteringMagnitude = totalStats.maxInScatteringMagnitude;
    result.meanInScatteringMagnitude = totalStats.texelCount > 0
        ? static_cast<float>(totalStats.sumInScatteringMagnitude / static_cast<double>(totalStats.texelCount))
        : 0.0f;

    // See this heuristic's own doc comment (AtmosphereAerialPerspectiveLutInspection.h) -
    // minTransmittance/maxInScatteringMagnitude already stand in for "the
    // farthest slice's own numbers" without needing separate per-slice
    // tracking, since transmittance only ever decreases (and in-scattering
    // magnitude only ever increases) with distance through the volume.
    const double hazeBudget = 1.0 - static_cast<double>(result.minTransmittance);
    const double scatterBudget = static_cast<double>(result.maxInScatteringMagnitude);
    result.likelyVisibleAtDefaultExposure = (hazeBudget + scatterBudget) >= kLikelyVisibleThreshold;

    return result;
}

AtmosphereAerialPerspectiveLutInspectionResult InspectAerialPerspectiveVolume(
    Renderer& renderer, AtmosphereLutRenderer& atmosphereLutRenderer, const char* aerialPerspectiveVolumeName)
{
    AtmosphereAerialPerspectiveLutInspectionResult result;

    const int depth = atmosphereLutRenderer.AerialPerspectiveVolumeDepth(aerialPerspectiveVolumeName);
    if (depth <= 0) {
        result.failureReason = std::string("\"") + aerialPerspectiveVolumeName
            + "\" has not been computed yet this session - render at least one frame first.";
        return result;
    }

    AerialPerspectiveSliceStats totalStats;
    int width = 0;
    int height = 0;

    for (std::uint32_t sliceIndex = 0; sliceIndex < static_cast<std::uint32_t>(depth); ++sliceIndex) {
        const Renderer::CapturedRawPixels raw = atmosphereLutRenderer.CaptureAerialPerspectiveVolumeSliceImmediate(
            renderer, aerialPerspectiveVolumeName, sliceIndex, kInspectionOutputTextureName);

        if (raw.width <= 0 || raw.height <= 0 || raw.pixels.empty()) {
            result.failureReason = std::string("\"") + aerialPerspectiveVolumeName
                + "\" slice capture returned no data (slice " + std::to_string(sliceIndex) + " of " + std::to_string(depth)
                + ") - the volume may not have been generated yet this session.";
            return result;
        }

        width = raw.width;
        height = raw.height;
        const std::size_t texelCount = static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height);
        totalStats = AccumulateAerialPerspectiveSliceStats(raw.pixels.data(), texelCount, totalStats);
    }

    return FinalizeAerialPerspectiveLutInspection(totalStats, width, height, depth);
}

} // namespace gte
