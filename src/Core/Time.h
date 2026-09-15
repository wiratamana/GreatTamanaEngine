#pragma once

#include <cstdint>

namespace gte {

// Explicit, dependency-injected time-keeping object - the frame-debugger/
// Unity-Pause campaign's own dedicated "Time" class (see
// task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md, Locked Design
// Decision #3). Deliberately NOT a singleton/static accessor - exactly one
// instance is owned by Application (Application::m_engineContext.time) and
// threaded down explicitly through EngineContext into whatever engine-core
// code needs it (Game::Update() as of PHASE2), matching this project's own
// Tier-1 testability philosophy (see AGENTS.md, "Testability & Regression
// Safety").
//
// Advance() is the ONLY way this class's state ever changes, and is called
// EXACTLY ONCE per real Application::Run() loop iteration, regardless of
// pause state - see PHASE4. Every other method here is a read-only
// accessor for whatever the last Advance() call computed.
class Time {
public:
    // Advances this object by exactly one real frame.
    //
    //   realDeltaSeconds   - actual wall-clock seconds elapsed since the
    //                        previous Advance() call (SDL_GetTicksNS()-
    //                        derived in production - see PHASE4). Always
    //                        applied to UnscaledDeltaTime()/
    //                        TimeSinceStartupSeconds() verbatim,
    //                        regardless of pause.
    //   isPaused           - this frame's pause/resume state, as decided
    //                        by whoever owns the Play/Pause UI (the
    //                        Editor toolbar - see PHASE3/PHASE4).
    //   isSteppedThisFrame - true only on the one frame a "Step" request
    //                        is being honored. Internally ANDed with
    //                        isPaused (a Step request is only ever
    //                        meaningful while paused - see
    //                        IsFrozenThisFrame() below), so passing true
    //                        here while isPaused is false is harmless,
    //                        not a caller contract violation.
    //   fixedStepSeconds   - the deterministic amount of simulated time a
    //                        single Step advances by, AND (see Locked
    //                        Design Decision #11) the amount used for the
    //                        one frame immediately after resuming from a
    //                        pause, instead of replaying the full elapsed
    //                        real-world pause duration as one giant catch-
    //                        up step. Must be > 0; production code always
    //                        passes a fixed 1/60 (see PHASE4).
    void Advance(double realDeltaSeconds, bool isPaused, bool isSteppedThisFrame, double fixedStepSeconds) noexcept;

    // The delta simulation/gameplay code should actually use (Unity's own
    // Time.deltaTime equivalent): realDeltaSeconds while running normally,
    // exactly fixedStepSeconds on a Step frame OR on the one frame
    // immediately after resuming from a pause, or exactly 0.0 while
    // frozen (paused, not stepping) - see IsFrozenThisFrame().
    double DeltaTime() const noexcept { return m_deltaSeconds; }

    // Always the real, wall-clock elapsed seconds since the last Advance()
    // call, REGARDLESS of pause - Unity's own Time.unscaledDeltaTime
    // equivalent. Exposed for any future pause-INDEPENDENT per-frame logic
    // (UI animations, a future frame-debugger overlay, ...) - nothing in
    // this campaign currently reads it (the Editor's Scene camera is
    // driven by raw per-pixel mouse deltas, not a time base at all - see
    // EditorCamera::Update()).
    double UnscaledDeltaTime() const noexcept { return m_unscaledDeltaSeconds; }

    bool IsPaused() const noexcept { return m_isPaused; }

    // True only on a frame where NO simulation work should run at all:
    // paused AND not currently honoring a Step request. False on every
    // normal running frame AND on a Step frame (a Step frame DOES advance
    // the simulation, by exactly fixedStepSeconds). This is the one flag
    // Game::Update() actually branches on - see PHASE2.
    bool IsFrozenThisFrame() const noexcept { return m_isPaused && !m_isSteppedThisFrame; }

    // True only on the exact frame a Step request is being honored (always
    // implies IsPaused() == true - see Advance()'s own isSteppedThisFrame
    // parameter doc comment above).
    bool IsSteppedThisFrame() const noexcept { return m_isSteppedThisFrame; }

    // Total REAL (unscaled) seconds elapsed since the very first Advance()
    // call - never frozen by pause, monotonically increasing every call.
    double TimeSinceStartupSeconds() const noexcept { return m_timeSinceStartupSeconds; }

    // Total SIMULATED seconds elapsed: the running sum of every past
    // DeltaTime() value - freezes while paused (not stepping), and jumps
    // forward by exactly fixedStepSeconds on each Step or resume-frame.
    double SimulatedTimeSeconds() const noexcept { return m_simulatedTimeSeconds; }

    // Increments by exactly 1 on every single Advance() call, regardless
    // of pause state - a real frame still happened, even a frozen one.
    std::uint64_t FrameCount() const noexcept { return m_frameCount; }

private:
    double m_deltaSeconds = 0.0;
    double m_unscaledDeltaSeconds = 0.0;
    double m_timeSinceStartupSeconds = 0.0;
    double m_simulatedTimeSeconds = 0.0;
    std::uint64_t m_frameCount = 0;
    bool m_isPaused = false;
    bool m_isSteppedThisFrame = false;

    // Locked Design Decision #11 - true if the PREVIOUS Advance() call
    // observed isPaused == true. Lets THIS call detect "we just resumed"
    // and clamp that one frame's DeltaTime() to fixedStepSeconds instead
    // of the full (possibly huge) realDeltaSeconds elapsed across the
    // entire pause.
    bool m_wasPausedLastCall = false;
};

} // namespace gte
