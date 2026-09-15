# PHASE1 — Completion Report: Core `Time` class and `EngineContext`

Phase file: `PHASE1_CORE_TIME_CLASS_AND_ENGINE_CONTEXT.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`

## Summary

Implemented PHASE1 exactly per its "Step 3: The Plan" — a brand-new,
self-contained, Tier-1-testable `src/Core/` module with no call sites into
the rest of the engine yet. Pure addition, zero behavior change anywhere
else.

## What was added

- **`src/Core/Time.h`** — `gte::Time`, an explicit (non-singleton)
  time-keeping class exactly matching the phase document's specified API:
  `Advance(realDeltaSeconds, isPaused, isSteppedThisFrame, fixedStepSeconds)`,
  `DeltaTime()`, `UnscaledDeltaTime()`, `IsPaused()`, `IsFrozenThisFrame()`,
  `IsSteppedThisFrame()`, `TimeSinceStartupSeconds()`,
  `SimulatedTimeSeconds()`, `FrameCount()`. Copied verbatim from the phase
  document's own header, including all doc comments.
- **`src/Core/Time.cpp`** — `Time::Advance()` implementation, copied
  verbatim from the phase document, including the Locked Design Decision
  #11 "just resumed from a pause" one-frame clamp (`m_wasPausedLastCall`
  latch).
- **`src/Core/EngineContext.h`** — `gte::EngineContext`, a minimal
  aggregate struct holding exactly one field, `Time time;`, copied verbatim
  from the phase document.
- **`tests/Core/TimeTests.cpp`** — new Tier-1 test file, 9 `TEST()` cases
  covering every numbered scenario in the phase document's "3.5" section:
  1. `DefaultConstructedTimeReadsAllZeroFalseDefaults` (case 9 in the plan)
  2. `NormalRunningFrameUsesRealDelta` (case 1)
  3. `PausedNotSteppingFreezesDeltaButKeepsUnscaledReal` (case 2)
  4. `PausedAndSteppedUsesFixedStepRegardlessOfRealDelta` (case 3, using a
     deliberately different real delta of `2.5` as the phase document
     itself suggests)
  5. `SteppedWhileNotPausedIsHarmlessAndBehavesLikeNormalFrame` (case 4)
  6. `ResumeAfterLongPauseClampsExactlyOneFrameToFixedStep` (case 5 — one
     normal frame, then a 5-second simulated pause, then a resume frame
     that must clamp to `1/60`, then a following ordinary frame that must
     NOT be clamped again)
  7. `TimeSinceStartupNeverFreezesRegardlessOfPauseState` (case 6)
  8. `SimulatedTimeFreezesOnlyWhilePausedAndNotStepping` (case 7)
  9. `FrameCountAlwaysIncrementsEvenWhileFrozen` (case 8)

  All 9 tests construct a plain `gte::Time` and drive it purely through
  hand-picked `Advance()` argument sequences — no ECS/GPU/SDL/ImGui
  dependency at all, matching `GpuResourceHandleTests.cpp`'s existing
  shape/precedent.

## Modified files

- **`CMakeLists.txt`** — added `src/Core/Time.h`, `src/Core/Time.cpp`,
  `src/Core/EngineContext.h` as the first three entries of
  `add_library(gte_core STATIC ...)`'s file list, immediately before the
  existing `src/Math/MathTypes.h` line, exactly as the phase document
  specifies.
- **`tests/CMakeLists.txt`** — added `Core/TimeTests.cpp` as the first
  entry of the `set(GTE_TEST_SOURCES ...)` list (immediately before
  `Memory/GpuResourceHandleTests.cpp`), unconditional, no feature-gate.
  Also added a matching entry to the file's own "Test taxonomy" comment
  block at the top, mirroring the style of every other listed test file
  (for documentation consistency only — not requested explicitly by the
  phase document, but a very small, low-risk addition that keeps the
  existing convention intact).

## No other files touched

Per the phase document's own "This phase touches nothing else in the
engine — no call site changes yet" — confirmed: `Game.h`/`.cpp`,
`Application.h`/`.cpp`, `Editor/*`, `AnimationSystem.h` are all untouched.
Those are PHASE2+ work.

## Compile/test check performed

1. **Scoped incremental build**:
   `cmake --build build --target GreatTamanaEngineTests`
   (working directory `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`)
   — succeeded with zero new warnings/errors. Rebuilt `gte_core` (only
   `Time.cpp` needed recompiling) and relinked
   `tests\GreatTamanaEngineTests.exe`.

2. **Scoped test run**:
   `tests\GreatTamanaEngineTests.exe --gtest_filter=*TimeTest*`
   (from the `build` directory)
   — result: **`[ PASSED ] 9 tests.`** (all 9 cases from `TimeTests.cpp`,
   0 failures).

3. Per the phase document's own instruction, a full `ctest` regression
   pass was deliberately **not** run in this phase (brand-new, isolated
   module with zero existing call sites — nothing else could regress).
   That is reserved for PHASE5.

## Notes for the next phase (PHASE2)

- `gte::Time`/`gte::EngineContext` are ready to be threaded into
  `Game::Update()`'s new signature exactly as PHASE0/PHASE2 describe —
  nothing here needs revisiting.
- No surprises or deviations from the phase document's plan were found;
  the plan's exact header/source text was used verbatim since it was
  already fully specified and self-consistent.

## Git

All new/modified files for this phase, plus this report, were staged and
committed together with a commit message referencing PHASE1.
