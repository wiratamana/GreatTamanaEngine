#pragma once

#include <cstdint>

namespace gte {

// Injected frame clock (not a singleton). Advance() is the only mutator and runs once per real frame,
// paused or not; every other method reads what the last Advance() computed.
class Time {
public:
    // Advances by one real frame.
    //   realDeltaSeconds   - wall-clock seconds since the previous call; always feeds UnscaledDeltaTime().
    //   isPaused           - pause state decided by the Play/Pause owner.
    //   isSteppedThisFrame - one-frame Step request; ignored unless isPaused.
    //   fixedStepSeconds   - simulated time of a Step and of the first frame after a resume (must be > 0).
    void Advance(double realDeltaSeconds, bool isPaused, bool isSteppedThisFrame, double fixedStepSeconds) noexcept;

    // The delta simulation code should use: the real delta while running, fixedStepSeconds on a Step frame
    // or the frame after resuming, and 0.0 while frozen (see IsFrozenThisFrame()).
    double DeltaTime() const noexcept { return m_deltaSeconds; }

    // Real wall-clock seconds since the last Advance(), regardless of pause (like Unity's unscaledDeltaTime).
    double UnscaledDeltaTime() const noexcept { return m_unscaledDeltaSeconds; }

    bool IsPaused() const noexcept { return m_isPaused; }

    // True when no simulation work should run: paused and not stepping.
    bool IsFrozenThisFrame() const noexcept { return m_isPaused && !m_isSteppedThisFrame; }

    // True only on the frame a Step request is honored (implies IsPaused()).
    bool IsSteppedThisFrame() const noexcept { return m_isSteppedThisFrame; }

    // Real seconds since the first Advance(); never frozen by pause.
    double TimeSinceStartupSeconds() const noexcept { return m_timeSinceStartupSeconds; }

    // Sum of every past DeltaTime(); freezes while paused.
    double SimulatedTimeSeconds() const noexcept { return m_simulatedTimeSeconds; }

    // Increments on every Advance(), frozen frames included.
    std::uint64_t FrameCount() const noexcept { return m_frameCount; }

private:
    double m_deltaSeconds = 0.0;
    double m_unscaledDeltaSeconds = 0.0;
    double m_timeSinceStartupSeconds = 0.0;
    double m_simulatedTimeSeconds = 0.0;
    std::uint64_t m_frameCount = 0;
    bool m_isPaused = false;
    bool m_isSteppedThisFrame = false;

    // True if the previous Advance() was paused: the first frame after a resume uses fixedStepSeconds.
    bool m_wasPausedLastCall = false;
};

} // namespace gte
