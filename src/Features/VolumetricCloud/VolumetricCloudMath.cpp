#include "VolumetricCloudTypes.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

float ClampOrDefault(float value, float low, float high, float fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

VolumetricCloudSettings SanitizeVolumetricCloudSettings(const VolumetricCloudSettings& candidate) noexcept
{
    const VolumetricCloudSettings defaults;
    VolumetricCloudSettings out = candidate;
    out.slice = ClampOrDefault(candidate.slice, 0.0f, 1.0f, defaults.slice);
    out.tiling = ClampOrDefault(candidate.tiling, 0.25f, 16.0f, defaults.tiling);
    out.channel = std::min(candidate.channel, kCloudDebugChannelRgb);
    if (static_cast<std::uint8_t>(candidate.debugView) > static_cast<std::uint8_t>(VolumetricCloudDebugView::BlueNoise)) {
        out.debugView = VolumetricCloudDebugView::Off;
    }
    return out;
}

VolumetricCloudSlicePush MakeVolumetricCloudSlicePush(const VolumetricCloudSettings& settings) noexcept
{
    return VolumetricCloudSlicePush{ settings.slice, settings.tiling, settings.channel, 0u };
}

} // namespace gte
