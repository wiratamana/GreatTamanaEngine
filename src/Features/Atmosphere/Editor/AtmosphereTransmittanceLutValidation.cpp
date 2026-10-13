#include "AtmosphereTransmittanceLutValidation.h"

#include "../AtmosphereMath.h"
#include "../../../Renderer/Renderer.h"
#include "../../../Renderer/RenderGraph/RenderGraph.h"
#include "../../../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace gte {

namespace {

// A LOCAL copy of AtmosphereCommon.glsl's RayIntersectsSphereNearest() (the
// same ground-occlusion check AtmosphereTransmittanceLut.comp itself calls
// per-texel) - deliberately NOT part of the permanent AtmosphereMath.h
// oracle, since that file's own doc comment explicitly scopes ground
// occlusion as "a LUT-shape-specific concern belonging to whichever later
// phase builds the real Transmittance LUT" (Phase 3's shader, not the
// general-purpose CPU oracle). Needed here so this validation tool
// reproduces the SAME per-texel decode the real shader performs -
// ground-hit texels included, not just the (easier) non-occluded ones.
float RayIntersectsSphereNearestForValidation(Vec3 originKm, Vec3 direction, float radiusKm) noexcept
{
    const float b = 2.0f * Dot(originKm, direction);
    const float c = Dot(originKm, originKm) - radiusKm * radiusKm;
    const float discriminant = b * b - 4.0f * c;
    if (discriminant < 0.0f) {
        return -1.0f;
    }
    const float sqrtDiscriminant = std::sqrt(discriminant);
    const float t0 = (-b - sqrtDiscriminant) * 0.5f;
    const float t1 = (-b + sqrtDiscriminant) * 0.5f;
    if (t0 < 0.0f && t1 < 0.0f) {
        return -1.0f;
    }
    if (t0 < 0.0f) {
        return std::max(t1, 0.0f);
    }
    if (t1 < 0.0f) {
        return std::max(t0, 0.0f);
    }
    return std::max(std::min(t0, t1), 0.0f);
}

// SAME fixed sample count as Shaders/AtmosphereTransmittanceLut.comp's own
// kTransmittanceLutSampleCount - kept in lock-step by hand (this campaign
// deliberately has no shader reflection - see AGENTS.md).
constexpr int kValidationTransmittanceSampleCount = 40;

double ByteToUnit(std::uint8_t byteValue) noexcept
{
    return static_cast<double>(byteValue) / 255.0;
}

} // namespace

std::string ToDiagnosticString(const AtmosphereTransmittanceLutValidationResult& result)
{
    if (!result.succeeded) {
        return "Atmosphere Transmittance LUT Validation: FAILED - " + result.failureReason;
    }

    char buffer[640];
    std::snprintf(buffer, sizeof(buffer),
        "Atmosphere Transmittance LUT Validation: %dx%d (%zu texels)\n"
        "  max per-channel delta:  %.6f\n"
        "  mean per-channel delta: %.6f\n"
        "  epsilon:                %.6f\n"
        "  texels exceeding epsilon: %zu / %zu",
        result.width, result.height, result.texelCount, result.maxChannelDelta, result.meanChannelDelta,
        result.epsilon, result.texelsExceedingEpsilon, result.texelCount);
    return std::string(buffer);
}

AtmosphereTransmittanceLutValidationResult ValidateAtmosphereTransmittanceLut(
    Renderer& renderer, const rg::RenderGraph& renderGraph, const AtmosphereParametersGpu& params, double epsilon)
{
    AtmosphereTransmittanceLutValidationResult result;
    result.epsilon = epsilon;

    // Owned by the render graph's persistent texture cache now, not a
    // Texture2D this feature holds directly - looked up by its exact,
    // stable registered name, mirroring ValidateAerialPerspectiveSkyPurity()'s
    // own DebugTextureSnapshotFor() precedent.
    const std::optional<rg::DebugTextureSnapshot> snapshot =
        renderGraph.DebugTextureSnapshotFor("AtmosphereTransmittanceLut");
    if (!snapshot.has_value()) {
        result.failureReason =
            "AtmosphereTransmittanceLut has not been computed yet this session - render at least one frame first.";
        return result;
    }

    result.width = static_cast<int>(snapshot->target.extent.width);
    result.height = static_cast<int>(snapshot->target.extent.height);
    result.texelCount = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
    if (result.texelCount == 0) {
        result.failureReason = "AtmosphereTransmittanceLut has a zero-sized dimension - nothing to validate.";
        return result;
    }

    const Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(
        snapshot->target.image, VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format, FromVkExtent2D(snapshot->target.extent),
        snapshot->colorState);

    double deltaSum = 0.0;
    double maxDelta = 0.0;
    std::size_t exceeding = 0;

    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const std::size_t pixelByteIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(result.width) + static_cast<std::size_t>(x))
                * 4;
            const double gpuR = ByteToUnit(raw.pixels[pixelByteIndex + 0]);
            const double gpuG = ByteToUnit(raw.pixels[pixelByteIndex + 1]);
            const double gpuB = ByteToUnit(raw.pixels[pixelByteIndex + 2]);

            // Plain LINEAR height/angle grid - x = texel/(width-1), y =
            // texel/(height-1) - matches AtmosphereTransmittanceLut.comp's
            // own per-texel decode EXACTLY (see that file's own doc
            // comment).
            const float u = (result.width > 1) ? static_cast<float>(x) / static_cast<float>(result.width - 1) : 0.0f;
            const float v = (result.height > 1) ? static_cast<float>(y) / static_cast<float>(result.height - 1) : 0.0f;

            float heightKm = 0.0f;
            float upDot = 0.0f;
            TransmittanceLutUvToHeightZenith(params, u, v, heightKm, upDot);

            const Vec3 viewDirection(0.0f, upDot, std::sqrt(std::max(1.0f - upDot * upDot, 0.0f)));
            const float planetRadiusKm = params.planetRadiusKm;
            const Vec3 positionKm(0.0f, heightKm + planetRadiusKm, 0.0f);

            const bool groundHit =
                RayIntersectsSphereNearestForValidation(positionKm, viewDirection, planetRadiusKm) >= 0.0f;

            const Vec3 expected = groundHit
                ? Vec3::Zero()
                : ComputeTransmittanceToTopOfAtmosphere(
                      params, positionKm, viewDirection, kValidationTransmittanceSampleCount);

            const double deltaR = std::abs(gpuR - static_cast<double>(expected.x));
            const double deltaG = std::abs(gpuG - static_cast<double>(expected.y));
            const double deltaB = std::abs(gpuB - static_cast<double>(expected.z));
            const double maxChannelDelta = std::max({ deltaR, deltaG, deltaB });

            deltaSum += maxChannelDelta;
            maxDelta = std::max(maxDelta, maxChannelDelta);
            if (maxChannelDelta > epsilon) {
                ++exceeding;
            }
        }
    }

    result.maxChannelDelta = maxDelta;
    result.meanChannelDelta = deltaSum / static_cast<double>(result.texelCount);
    result.texelsExceedingEpsilon = exceeding;
    result.succeeded = true;
    return result;
}

} // namespace gte
