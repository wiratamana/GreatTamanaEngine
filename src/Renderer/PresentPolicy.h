#pragma once

// Presentation pacing and the pure choice of a present mode. Vulkan-free.

#include <cstdint>

namespace gte {

// Presentation pacing. Capped is the default.
enum class PresentPolicy : std::uint8_t { Capped, Uncapped };

enum class PresentMode : std::uint8_t { Fifo, Mailbox, Immediate };

// Fifo is always available, so it has no flag.
struct SupportedPresentModes {
    bool mailbox = false;
    bool immediate = false;
};

// Capped -> Fifo. Uncapped -> Mailbox, else Immediate, else Fifo.
constexpr PresentMode ChoosePresentMode(PresentPolicy policy, SupportedPresentModes supported) noexcept
{
    if (policy == PresentPolicy::Capped) {
        return PresentMode::Fifo;
    }
    if (supported.mailbox) {
        return PresentMode::Mailbox;
    }
    return supported.immediate ? PresentMode::Immediate : PresentMode::Fifo;
}

constexpr const char* PresentPolicyName(PresentPolicy policy) noexcept
{
    return policy == PresentPolicy::Uncapped ? "uncapped" : "capped";
}

constexpr const char* PresentModeName(PresentMode mode) noexcept
{
    switch (mode) {
    case PresentMode::Fifo: return "fifo";
    case PresentMode::Mailbox: return "mailbox";
    case PresentMode::Immediate: return "immediate";
    }
    return "fifo";
}

} // namespace gte
