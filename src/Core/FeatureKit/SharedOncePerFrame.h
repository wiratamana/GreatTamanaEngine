#pragma once

#include <cstdint>

namespace gte::fx {

// Remembers the last frame a once-per-frame callback handled.
// A callback that threw is marked failed, not retried in that frame: a retry would redeclare half-made passes.
class SharedOncePerFrame {
public:
    enum class State : std::uint8_t { Idle, Done, Failed };

    // True when the callback has not been handled for `frame` yet.
    bool ShouldRun(std::uint64_t frame) const noexcept { return m_state == State::Idle || m_frame != frame; }

    // Call only after the callback returned normally.
    void MarkDone(std::uint64_t frame) noexcept
    {
        m_state = State::Done;
        m_frame = frame;
    }

    // Call when the callback threw.
    void MarkFailed(std::uint64_t frame) noexcept
    {
        m_state = State::Failed;
        m_frame = frame;
    }

private:
    State m_state = State::Idle;
    std::uint64_t m_frame = 0;
};

} // namespace gte::fx
