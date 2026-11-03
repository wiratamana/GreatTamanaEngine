#pragma once

#include "BloomTypes.h"
#include "../../Core/Extent.h"

#include <cstdint>

namespace gte {

// Clamps every field into its valid range. A non-finite float becomes the default.
BloomSettings SanitizeBloomSettings(const BloomSettings& settings) noexcept;

// Size of mip `level` (0 = half the source). Each level halves the previous one, rounding up; zero for an empty source.
Extent2D BloomMipExtent(Extent2D source, std::uint32_t level) noexcept;

// Chain length actually used: `requested` reduced until the last mip is big enough. 0 when even the minimum does not fit.
std::uint32_t BloomActiveLevels(Extent2D source, std::uint32_t requested) noexcept;

// Share of a pixel kept by the soft threshold: 0 below the knee, 1 well above the threshold.
// Same curve as BloomPrefilter.comp. `absoluteKnee` is threshold * knee.
float BloomSoftThresholdWeight(float brightness, float threshold, float absoluteKnee) noexcept;

// `safe` must be sanitized.
BloomPrefilterPush MakeBloomPrefilterPush(const BloomSettings& safe) noexcept;

// `finalLevel` applies the intensity; inner levels pass 1.
BloomUpPush MakeBloomUpPush(const BloomSettings& safe, bool finalLevel) noexcept;

} // namespace gte
