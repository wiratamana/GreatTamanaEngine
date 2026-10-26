#pragma once

#include <cstdint>

namespace gte {

// Delays a requested exit until the reply of the request has left the process.
// Due once both a frame count and a time have passed since the first Arm().
struct ExitGate {
    static constexpr std::uint64_t kFrames = 3;
    static constexpr std::uint64_t kNanoseconds = 100'000'000;

    bool armed = false;
    std::uint64_t frame = 0;
    std::uint64_t ns = 0;

    // The first call fixes the deadline; later calls keep it.
    void Arm(std::uint64_t currentFrame, std::uint64_t nowNs) noexcept
    {
        if (armed) {
            return;
        }
        armed = true;
        frame = currentFrame;
        ns = nowNs;
    }

    bool Due(std::uint64_t currentFrame, std::uint64_t nowNs) const noexcept
    {
        return armed && currentFrame >= frame + kFrames && nowNs >= ns + kNanoseconds;
    }
};

} // namespace gte
