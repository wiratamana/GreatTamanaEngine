#pragma once

#include <cstdint>
#include <limits>

namespace gte::rg {

// Frame-driven gate for lazy builds that can fail. A failure backs off with a doubling cooldown
// and never gives up; the caller logs on First / recovery only.
class BuildRetryGate {
public:
    static constexpr std::uint64_t kInitialCooldownFrames = 120;
    static constexpr std::uint64_t kMaxCooldownFrames = 1800;

    enum class FailureKind : std::uint8_t { First, Repeat };

    // True when the build never failed, or its cooldown has elapsed.
    bool ShouldAttempt(std::uint64_t frame) const noexcept { return !m_failing || frame >= m_nextAttemptFrame; }

    FailureKind OnFailure(std::uint64_t frame) noexcept
    {
        const FailureKind kind = m_failing ? FailureKind::Repeat : FailureKind::First;
        m_cooldown = m_failing ? NextCooldown(m_cooldown) : kInitialCooldownFrames;
        m_nextAttemptFrame = SaturatingAdd(frame, m_cooldown);
        m_failing = true;
        return kind;
    }

    // True when this success recovers from an earlier failure.
    bool OnSuccess() noexcept
    {
        const bool recovered = m_failing;
        m_failing = false;
        m_cooldown = 0;
        m_nextAttemptFrame = 0;
        return recovered;
    }

private:
    static constexpr std::uint64_t NextCooldown(std::uint64_t cooldown) noexcept
    {
        return cooldown >= kMaxCooldownFrames / 2 ? kMaxCooldownFrames : cooldown * 2;
    }

    static constexpr std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b) noexcept
    {
        return a > std::numeric_limits<std::uint64_t>::max() - b ? std::numeric_limits<std::uint64_t>::max() : a + b;
    }

    bool m_failing = false;
    std::uint64_t m_cooldown = 0;
    std::uint64_t m_nextAttemptFrame = 0;
};

} // namespace gte::rg
