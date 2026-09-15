# PHASE1 — Core `Time` class and `EngineContext` (new `src/Core/` module)

Parent: `PHASE0_MASTER_STRATEGY.md` — read it first, especially "Locked
Design Decisions" #2, #3, #4, #11.

## Step 1: The Goal

Add a brand-new, self-contained, **Tier-1-testable** `src/Core/` module
containing:

1. `gte::Time` — an explicit (never singleton/static) time-keeping object
   that is the single source of truth for "how much simulated time passed
   this frame", "are we paused", "are we stepping", and simple bookkeeping
   (frame count, total simulated/real time). Nothing outside this class
   ever mutates its state except through its one `Advance()` method.
2. `gte::EngineContext` — a small, deliberately minimal aggregate struct
   holding a `Time time;` member, meant to be threaded down explicitly
   into whatever engine-core code needs frame-timing information (starting
   with `Game::Update()` in PHASE2).

This phase touches **nothing else** in the engine — no call site changes
yet. It is pure addition, safe to land completely independently, and
should compile clean and pass its own new tests before PHASE2 begins.

## Step 2: The Situation

- No `Time` or `EngineContext` type exists anywhere in this codebase today.
- `src/` has no `Core/` folder yet — every other top-level module
  (`Math/`, `ECS/`, `Physics/`, ...) is a sibling directory directly under
  `src/`. `Core/` will be a new one, at the same level.
- Every source file is explicitly listed in `CMakeLists.txt`'s
  `add_library(gte_core STATIC ...)` call — no globbing. New files must be
  added there by hand.
- Every Tier-1 test file is explicitly listed in `tests/CMakeLists.txt`'s
  `GTE_TEST_SOURCES` list — same rule.
- This project's convention for a small, pure, always-compiled, testable
  module with no GPU/ImGui/SDL dependency is well established — e.g.
  `src/Physics/FixedTimestepAccumulator.h/.cpp` (a similarly-scoped, tiny,
  pure module) is the closest existing precedent to copy the *shape* of
  (a `.h` with the class declaration and thorough doc comments, a small
  `.cpp` with the actual logic, and a single dedicated test file).

## Step 3: The Plan

### 3.1 `src/Core/Time.h`

```cpp
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
```

### 3.2 `src/Core/Time.cpp`

```cpp
#include "Time.h"

namespace gte {

void Time::Advance(double realDeltaSeconds, bool isPaused, bool isSteppedThisFrame, double fixedStepSeconds) noexcept
{
    m_unscaledDeltaSeconds = realDeltaSeconds;
    m_timeSinceStartupSeconds += realDeltaSeconds;
    ++m_frameCount;

    m_isPaused = isPaused;
    // A Step request is only ever meaningful while paused - see the
    // header's own doc comment on this parameter.
    m_isSteppedThisFrame = isPaused && isSteppedThisFrame;

    if (m_isSteppedThisFrame) {
        m_deltaSeconds = fixedStepSeconds;
    } else if (m_isPaused) {
        m_deltaSeconds = 0.0;
    } else if (m_wasPausedLastCall) {
        // Locked Design Decision #11 - just resumed from a pause of
        // arbitrary real-world duration. Nothing was simulated while
        // frozen, so don't replay that entire elapsed wall-clock gap as
        // one giant catch-up step (which could tunnel a fast-moving
        // physics object clean through a thin collider, or make an
        // animation jump forward by however long the user stepped away
        // for) - resume with a single ordinary-sized step instead, exactly
        // as if merely one normal frame had elapsed.
        m_deltaSeconds = fixedStepSeconds;
    } else {
        m_deltaSeconds = realDeltaSeconds;
    }

    m_simulatedTimeSeconds += m_deltaSeconds;
    m_wasPausedLastCall = m_isPaused;
}

} // namespace gte
```

### 3.3 `src/Core/EngineContext.h`

```cpp
#pragma once

#include "Time.h"

namespace gte {

// Small, explicit, extensible aggregate of "things the engine's own
// gameplay/simulation layer needs every frame" (see
// task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md, Locked Design
// Decision #3). Deliberately minimal today - just `time` - NOT a dumping
// ground for every engine subsystem: a future genuine need (e.g. a
// frame-debugger "currently inspected pass" handle) should be added here
// explicitly, one real, justified field at a time, exactly like
// src/Editor/EditorContext.h's own header comment already documents for
// that struct's fields.
//
// Owned by Application (Application::m_engineContext - see PHASE2/PHASE4),
// advanced exactly once per frame via `time.Advance(...)`, and passed down
// by const reference into Game::Update() - never copied around casually,
// and never mutated by anything except Application::Run() itself.
struct EngineContext {
    Time time;
};

} // namespace gte
```

### 3.4 `CMakeLists.txt` edit

Add the two new files to `add_library(gte_core STATIC ...)`'s file list.
Simplest, lowest-risk insertion point: immediately before the existing
`src/Math/MathTypes.h` line (i.e. as the very first two entries in the
list) — `Core/` is conceptually below even `Math/` (no dependency on it),
so this ordering communicates that correctly, though CMake itself does not
care about list order for a STATIC library.

```cmake
add_library(gte_core STATIC
    src/Core/Time.h
    src/Core/Time.cpp
    src/Core/EngineContext.h
    src/Math/MathTypes.h
    ...
```

### 3.5 New test file: `tests/Core/TimeTests.cpp`

Pure Tier-1 (no ECS/GPU/SDL/ImGui at all — just constructs a `gte::Time`
and calls `Advance()` with hand-picked values). Cover at least:

1. **Normal running frame**: `Advance(1.0/60.0, false, false, 1.0/60.0)` →
   `DeltaTime() == 1.0/60.0`, `UnscaledDeltaTime() == 1.0/60.0`,
   `IsPaused() == false`, `IsFrozenThisFrame() == false`,
   `IsSteppedThisFrame() == false`.
2. **Paused, not stepping**: after a normal frame, call
   `Advance(1.0/60.0, true, false, 1.0/60.0)` → `DeltaTime() == 0.0`,
   `UnscaledDeltaTime() == 1.0/60.0` (still real!), `IsPaused() == true`,
   `IsFrozenThisFrame() == true`.
3. **Paused + stepped**: call `Advance(anyRealDelta, true, true, 1.0/60.0)`
   → `DeltaTime() == 1.0/60.0` exactly (NOT `anyRealDelta`, proving Step is
   deterministic regardless of real elapsed time — try this with
   `anyRealDelta` set to something deliberately different, e.g. `2.5`, to
   make the regression meaningful), `IsPaused() == true`,
   `IsFrozenThisFrame() == false`, `IsSteppedThisFrame() == true`.
4. **`isSteppedThisFrame=true` while `isPaused=false` is harmless**: call
   `Advance(x, false, true, 1.0/60.0)` → behaves EXACTLY like a normal
   running frame (`DeltaTime() == x`, `IsSteppedThisFrame() == false`,
   `IsFrozenThisFrame() == false`) — proves the internal AND-with-isPaused
   guard works.
5. **Resume-after-pause clamping (Locked Design Decision #11)**: sequence
   — one normal frame, then `Advance(5.0, true, false, 1.0/60.0)` (a long
   simulated pause), then `Advance(5.0, false, false, 1.0/60.0)` (resume,
   5 real seconds elapsed since the last check, e.g. the user took a while
   to click Resume) → this THIRD call's `DeltaTime()` must be exactly
   `1.0/60.0`, **not** `5.0`. A FOURTH call, `Advance(1.0/60.0, false,
   false, 1.0/60.0)` (an ordinary frame right after resuming) must go back
   to normal (`DeltaTime() == 1.0/60.0`, taken from `realDeltaSeconds`, not
   clamped again) — proving the clamp only ever applies to the single
   frame immediately after a pause, never any frame after that.
6. **`TimeSinceStartupSeconds()` never freezes**: across a mixed sequence
   of running/paused/stepped `Advance()` calls, assert it equals the exact
   running sum of every `realDeltaSeconds` argument passed in, regardless
   of pause state.
7. **`SimulatedTimeSeconds()` freezes correctly**: across the same mixed
   sequence, assert it equals the exact running sum of every observed
   `DeltaTime()` result (i.e. it does NOT advance during a frozen — paused,
   non-stepped — frame, but DOES advance by `fixedStepSeconds` on a Step
   frame and on the one resume frame).
8. **`FrameCount()` always increments**: across the same mixed sequence
   (including frozen frames), assert it increments by exactly 1 per
   `Advance()` call, with no gaps.
9. **Default-constructed `Time`** (before any `Advance()` call): every
   accessor returns its documented zero/false default — never
   uninitialized/garbage values.

### 3.6 `tests/CMakeLists.txt` edit

Add `Core/TimeTests.cpp` to the `set(GTE_TEST_SOURCES ...)` list —
unconditional (not gated behind `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL`
/anything else), alongside e.g. `Memory/GpuResourceHandleTests.cpp` near the
top of the list, since `Time`/`EngineContext` have no such dependency
either.

### 3.7 Compile/test check for this phase

1. `cmake --build build` (or the project's normal incremental build
   command) — must succeed with zero new warnings/errors.
2. Build and run just the new test binary/filter, e.g.:
   `GreatTamanaEngineTests.exe --gtest_filter=*TimeTest*` — every new case
   from 3.5 above must pass.
3. Do **not** run a full `ctest` regression yet (that's PHASE5) — this
   phase adds a brand-new, isolated module with zero existing call sites,
   so nothing else could possibly regress from it. A full run costs time
   for no informational gain here.

### 3.8 Report

Write a short `PHASE1_COMPLETION_REPORT.md` in this same
`task_manager/frame-debugger-1/` folder summarizing what was added, the
exact test filter run and its result, and `git commit` everything
(`src/Core/Time.h`, `src/Core/Time.cpp`, `src/Core/EngineContext.h`,
`tests/Core/TimeTests.cpp`, the two `CMakeLists.txt` edits, and the report
itself).
