#pragma once

// How many offscreen frames the main loop keeps in flight. Vulkan-free.

#include "GpuTiming.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace gte {

static_assert(kGpuFramesInFlight < 10, "the option is one digit");

// 1 submits the main offscreen frame and waits for it; more lets the CPU build the next frame meanwhile.
inline constexpr std::uint32_t kDefaultOffscreenFramesInFlight = 1;

// Accepts "1" through kGpuFramesInFlight; anything else gives nullopt.
constexpr std::optional<std::uint32_t> ParseOffscreenFramesInFlight(std::string_view text) noexcept
{
    if (text.size() != 1 || text[0] < '1' || text[0] > static_cast<char>('0' + kGpuFramesInFlight)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(text[0] - '0');
}

} // namespace gte
