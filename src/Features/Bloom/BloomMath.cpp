#include "BloomMath.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

float SanitizeFloat(float value, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

} // namespace

BloomSettings SanitizeBloomSettings(const BloomSettings& settings) noexcept
{
    const BloomSettings d{};
    BloomSettings s = settings;
    s.threshold = SanitizeFloat(settings.threshold, 0.0f, 16.0f, d.threshold);
    s.knee = SanitizeFloat(settings.knee, 0.0f, 1.0f, d.knee);
    s.intensity = SanitizeFloat(settings.intensity, 0.0f, 4.0f, d.intensity);
    s.levels = std::clamp(settings.levels, kBloomMinLevels, kBloomMaxLevels);
    return s;
}

Extent2D BloomMipExtent(Extent2D source, std::uint32_t level) noexcept
{
    if (source.width == 0 || source.height == 0) {
        return Extent2D{};
    }
    const std::uint32_t shift = std::min(level, 30u) + 1u;
    const std::uint64_t round = (std::uint64_t{ 1 } << shift) - 1u;
    return Extent2D{ static_cast<std::uint32_t>((source.width + round) >> shift),
        static_cast<std::uint32_t>((source.height + round) >> shift) };
}

std::uint32_t BloomActiveLevels(Extent2D source, std::uint32_t requested) noexcept
{
    for (std::uint32_t levels = std::clamp(requested, kBloomMinLevels, kBloomMaxLevels); levels >= kBloomMinLevels; --levels) {
        const Extent2D last = BloomMipExtent(source, levels - 1);
        if (std::min(last.width, last.height) >= kBloomMinMipSize) {
            return levels;
        }
    }
    return 0;
}

float BloomSoftThresholdWeight(float brightness, float threshold, float absoluteKnee) noexcept
{
    const float soft = std::clamp(brightness - threshold + absoluteKnee, 0.0f, 2.0f * absoluteKnee);
    const float curve = soft * soft / (4.0f * absoluteKnee + 1e-5f);
    return std::max(curve, brightness - threshold) / std::max(brightness, 1e-5f);
}

BloomPrefilterPush MakeBloomPrefilterPush(const BloomSettings& safe) noexcept
{
    BloomPrefilterPush p;
    p.thresholdKneeMax[0] = safe.threshold;
    p.thresholdKneeMax[1] = safe.threshold * safe.knee;
    p.thresholdKneeMax[2] = kBloomMaxRadiance;
    return p;
}

BloomUpPush MakeBloomUpPush(const BloomSettings& safe, bool finalLevel) noexcept
{
    BloomUpPush p;
    p.scaleAndPad[0] = finalLevel ? safe.intensity : 1.0f;
    return p;
}

} // namespace gte
