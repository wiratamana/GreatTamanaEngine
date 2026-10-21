#include "SsgiMath.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

constexpr Vec3 kLumaWeights{ 0.2126f, 0.7152f, 0.0722f };
constexpr float kMinProjectedDepth = 1e-3f;
constexpr float kAlbedoChromaMin = 0.25f;
constexpr float kAlbedoChromaMax = 4.0f;
constexpr float kAlbedoMax = 0.95f;

float Fract(float v) noexcept
{
    return v - std::floor(v);
}

float SmoothStep(float edge0, float edge1, float x) noexcept
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Smoothstep needs distinct edges, so a zero fade width still spans a sliver.
float EndFadeStart(float maxDistance, float endFadeFraction) noexcept
{
    return maxDistance * std::min(1.0f - endFadeFraction, 0.999f);
}

} // namespace

SsgiSettings SanitizeSsgiSettings(const SsgiSettings& settings) noexcept
{
    constexpr SsgiSettings d{};
    SsgiSettings s = settings;

    s.intensity = fx::SanitizeFinite(settings.intensity, 0.0f, 4.0f, d.intensity);
    s.maxRayDistance = fx::SanitizeFinite(settings.maxRayDistance, 0.1f, 100.0f, d.maxRayDistance);
    s.thickness = fx::SanitizeFinite(settings.thickness, 0.01f, 5.0f, d.thickness);
    s.depthBias = fx::SanitizeFinite(settings.depthBias, 0.0f, 1.0f, d.depthBias);
    s.rayCount = std::clamp<std::uint32_t>(settings.rayCount, 1u, 8u);
    s.stepCount = std::clamp<std::uint32_t>(settings.stepCount, 4u, 48u);
    s.stepDistributionPower = fx::SanitizeFinite(settings.stepDistributionPower, 1.0f, 4.0f, d.stepDistributionPower);
    s.endFadeFraction = fx::SanitizeFinite(settings.endFadeFraction, 0.0f, 0.9f, d.endFadeFraction);
    s.maxRadiance = fx::SanitizeFinite(settings.maxRadiance, 0.5f, 64.0f, d.maxRadiance);
    s.albedoLuma = fx::SanitizeFinite(settings.albedoLuma, 0.0f, kAlbedoMax, d.albedoLuma);
    s.albedoSaturation = fx::SanitizeFinite(settings.albedoSaturation, 0.0f, 1.0f, d.albedoSaturation);
    s.maxDistance = fx::SanitizeFinite(settings.maxDistance, 1.0f, 100000.0f, d.maxDistance);
    s.distanceFadeFraction = fx::SanitizeFinite(settings.distanceFadeFraction, 0.0f, 0.9f, d.distanceFadeFraction);
    s.resolutionDivisor = settings.resolutionDivisor <= 1u ? 1u : 2u;
    s.historyWeight = fx::SanitizeFinite(settings.historyWeight, 0.0f, 0.98f, d.historyWeight);
    s.depthSigma = fx::SanitizeFinite(settings.depthSigma, 0.001f, 0.5f, d.depthSigma);
    s.debugView = static_cast<std::uint8_t>(settings.debugView) <= static_cast<std::uint8_t>(SsgiDebugView::Normals)
        ? settings.debugView
        : SsgiDebugView::Off;
    return s;
}

Vec3 SsgiEyeRelativePoint(float px, float py, float viewZ, Extent2D view, const fx::CameraBasis& basis) noexcept
{
    const float ox = (px + 0.5f - 0.5f * static_cast<float>(view.width)) * basis.pixelScale;
    const float oy = (py + 0.5f - 0.5f * static_cast<float>(view.height)) * basis.pixelScale;
    return (basis.forward + basis.right * ox + basis.down * oy) * viewZ;
}

SsgiProjection SsgiProjectToPixel(const Vec3& eyeRelative, Extent2D view, const fx::CameraBasis& basis) noexcept
{
    SsgiProjection out;
    out.viewZ = Dot(eyeRelative, basis.forward);
    if (out.viewZ < kMinProjectedDepth) {
        return out;
    }
    const float inv = 1.0f / (out.viewZ * basis.pixelScale);
    out.x = 0.5f * static_cast<float>(view.width) + Dot(eyeRelative, basis.right) * inv - 0.5f;
    out.y = 0.5f * static_cast<float>(view.height) + Dot(eyeRelative, basis.down) * inv - 0.5f;
    out.valid = true;
    return out;
}

Vec3 SsgiCosineHemisphereDirection(float u0, float u1, const Vec3& n) noexcept
{
    const Vec3 up = std::abs(n.y) < 0.99f ? Vec3::Up() : Vec3::Right();
    const Vec3 t = Normalize(Cross(up, n));
    const Vec3 b = Cross(n, t);
    const float r = std::sqrt(u0);
    const float phi = 2.0f * kPi * u1;
    return Normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(1.0f - u0, 0.0f)));
}

float SsgiInterleavedGradientNoise(float x, float y) noexcept
{
    return Fract(52.9829189f * Fract(x * 0.06711056f + y * 0.00583715f));
}

SsgiRayNoise SsgiRayNoiseFor(int px, int py, std::uint32_t noiseFrame, std::uint32_t rayCount,
    std::uint32_t rayIndex) noexcept
{
    const float x = static_cast<float>(px);
    const float y = static_cast<float>(py);
    const float base0 = SsgiInterleavedGradientNoise(x, y);
    const float base1 = SsgiInterleavedGradientNoise(y + 5.3f, x + 17.1f);
    const float jitterBase = SsgiInterleavedGradientNoise(x * 1.37f + 3.1f, y * 1.37f + 5.7f);
    const float n = static_cast<float>(noiseFrame * rayCount + rayIndex);

    SsgiRayNoise out;
    out.u0 = Fract(base0 + n * 0.7548776662f);
    out.u1 = Fract(base1 + n * 0.5698402910f);
    out.jitter = Fract(jitterBase + n * 0.6180339887f);
    return out;
}

float SsgiRayOriginOffset(float depthBias, float viewZ, float pixelScale, std::uint32_t divisor) noexcept
{
    return std::max(depthBias, kSsgiFootprintBiasScale * viewZ * pixelScale * static_cast<float>(divisor));
}

SsgiMarchResult SsgiMarchReference(const SsgiGrid& grid, const fx::CameraBasis& basis, Extent2D viewExtent,
    const Vec3& origin, const Vec3& dir, float jitter, const SsgiSettings& safe, float bias) noexcept
{
    SsgiMarchResult result;
    const float maxDist = safe.maxRayDistance;
    const float fadeStart = EndFadeStart(maxDist, safe.endFadeFraction);
    const float divisor = static_cast<float>(safe.resolutionDivisor);
    const int steps = static_cast<int>(safe.stepCount);
    const int width = static_cast<int>(grid.size.width);
    const int height = static_cast<int>(grid.size.height);
    if (grid.texels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return result;
    }

    float previousZ = Dot(origin, basis.forward);
    bool wasInFront = true;
    for (int i = 0; i < steps; ++i) {
        const float t =
            std::pow((static_cast<float>(i) + jitter) / static_cast<float>(steps), safe.stepDistributionPower) * maxDist;
        const SsgiProjection p = SsgiProjectToPixel(origin + dir * t, viewExtent, basis);
        if (!p.valid) {
            break;
        }
        const int qx = static_cast<int>(std::floor(p.x / divisor + 0.5f));
        const int qy = static_cast<int>(std::floor(p.y / divisor + 0.5f));
        if (qx < 0 || qy < 0 || qx >= width || qy >= height) {
            break;
        }

        const float advance = std::max(p.viewZ - previousZ, 0.0f);
        previousZ = p.viewZ;
        const std::size_t index = static_cast<std::size_t>(qy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(qx);
        const SsgiGridTexel& surface = grid.texels[index];
        if (surface.viewZ <= 0.0f) {
            wasInFront = true;
            continue;
        }

        const float diff = p.viewZ - surface.viewZ;
        const float reach = safe.thickness + (wasInFront ? advance : 0.0f);
        wasInFront = diff <= bias;
        if (diff > bias && diff < reach) {
            if (Dot(surface.normal, dir) >= 0.0f) {
                result.blocked = true;
                return result;
            }
            result.hit = true;
            result.texelX = qx;
            result.texelY = qy;
            result.distance = t;
            result.radiance = surface.radiance * (1.0f - SmoothStep(fadeStart, maxDist, t));
            return result;
        }
    }
    return result;
}

Vec3 SsgiAlbedoGuess(const Vec3& sceneColor, float luma, float saturation) noexcept
{
    const float lum = Dot(sceneColor, kLumaWeights);
    Vec3 chroma = lum > 1e-4f ? sceneColor / lum : Vec3::One();
    chroma = Lerp(Vec3::One(), chroma, saturation);
    chroma /= std::max(Dot(chroma, kLumaWeights), 1e-4f);

    const auto clampChroma = [](float v) { return std::clamp(v, kAlbedoChromaMin, kAlbedoChromaMax); };
    const auto clampAlbedo = [](float v) { return std::clamp(v, 0.0f, kAlbedoMax); };
    return Vec3{ clampAlbedo(luma * clampChroma(chroma.x)), clampAlbedo(luma * clampChroma(chroma.y)),
        clampAlbedo(luma * clampChroma(chroma.z)) };
}

SsgiPrepareParamsGpu MakeSsgiPrepareParams(
    const fx::CameraBasis& basis, const SsgiSettings& safe, const Mat4& invViewProjection) noexcept
{
    SsgiPrepareParamsGpu p;
    p.invViewProjection = invViewProjection;
    fx::PackVec3(p.eye, basis.eye);
    fx::PackVec3(p.forward, basis.forward);
    p.tuning[0] = safe.maxRadiance;
    p.counts[0] = safe.resolutionDivisor;
    return p;
}

SsgiTraceParamsGpu MakeSsgiTraceParams(const fx::CameraBasis& basis, const SsgiSettings& safe, Extent2D viewExtent,
    std::uint32_t noiseFrame) noexcept
{
    SsgiTraceParamsGpu p;
    fx::PackVec3(p.rightAndMaxDistance, basis.right, safe.maxRayDistance);
    fx::PackVec3(p.downAndThickness, basis.down, safe.thickness);
    fx::PackVec3(p.forwardAndBias, basis.forward, safe.depthBias);

    const float fadeEnd = safe.maxDistance;
    p.viewAndFade[0] = static_cast<float>(viewExtent.width);
    p.viewAndFade[1] = static_cast<float>(viewExtent.height);
    p.viewAndFade[2] = std::min(fadeEnd * (1.0f - safe.distanceFadeFraction), fadeEnd * 0.999f);
    p.viewAndFade[3] = fadeEnd;

    p.tuning[0] = safe.stepDistributionPower;
    p.tuning[1] = safe.endFadeFraction;
    p.tuning[2] = basis.pixelScale;

    p.counts[0] = safe.rayCount;
    p.counts[1] = safe.stepCount;
    p.counts[2] = noiseFrame;
    p.counts[3] = safe.resolutionDivisor;
    return p;
}

SsgiCompositePush MakeSsgiCompositePush(const fx::CameraBasis& basis, const SsgiSettings& safe,
    const Mat4& invViewProjection, std::uint32_t divisor) noexcept
{
    SsgiCompositePush p;
    p.invViewProjection = invViewProjection;
    fx::PackVec3(p.eye, basis.eye);
    fx::PackVec3(p.forward, basis.forward);
    p.intensityAlbedo[0] = safe.intensity;
    p.intensityAlbedo[1] = safe.albedoLuma;
    p.intensityAlbedo[2] = safe.albedoSaturation;
    p.intensityAlbedo[3] = safe.depthSigma;
    p.divisorDebug[0] = divisor;
    p.divisorDebug[1] = static_cast<std::uint32_t>(safe.debugView);
    return p;
}

} // namespace gte
