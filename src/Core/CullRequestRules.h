#pragma once

// Pure rules for the per-view CPU draw cull request. Vulkan-free so they are testable without a device.

#include <cstdint>
#include <limits>

namespace gte {

// Cull counts are live only when they come from the previous frame or the current one.
constexpr bool SceneCullCountsFresh(std::uint64_t storedFrame, std::uint64_t currentFrame) noexcept
{
    return storedFrame == currentFrame || (currentFrame > 0 && storedFrame == currentFrame - 1);
}

// Cull padding as a fraction of the largest box extent: clamped to [0, 1]; NaN, negative or infinite gives 0.
constexpr float SanitizeCullPadding(float padding) noexcept
{
    if (!(padding >= 0.0f) || padding > std::numeric_limits<float>::max()) {
        return 0.0f;
    }
    return padding > 1.0f ? 1.0f : padding;
}

// Aspect for a primary camera built without primary view data: the last primary aspect, else `callerAspect`, else 1.
constexpr float PrimaryAspectFallback(float lastPrimaryAspect, float callerAspect) noexcept
{
    const auto usable = [](float aspect) { return aspect > 0.0f && aspect <= std::numeric_limits<float>::max(); };
    if (usable(lastPrimaryAspect)) {
        return lastPrimaryAspect;
    }
    return usable(callerAspect) ? callerAspect : 1.0f;
}

} // namespace gte
