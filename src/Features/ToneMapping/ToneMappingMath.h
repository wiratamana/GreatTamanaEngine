#pragma once

#include "ToneMappingTypes.h"

#include <cstdint>

namespace gte {

// Clamps every field into its valid range. A non-finite float or an unknown mode becomes the default.
ToneMappingSettings SanitizeToneMappingSettings(const ToneMappingSettings& settings) noexcept;

// `safe` must be sanitized.
ToneMappingPush MakeToneMappingPush(const ToneMappingSettings& safe, std::uint64_t frameIndex) noexcept;

} // namespace gte
