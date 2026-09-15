# `frame-debugger-1` — Campaign Completion Report: Unity-style Pause/Step + a dedicated `Time` class

Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`
Phases: 5 (all landed)

## Goal recap

Give GreatTamanaEngine a genuine, Unity-style **Pause** capability for its
Editor — a Pause/Resume toggle and a Step button in the Editor UI; while
paused, gameplay simulation (skeletal animation, IK, physics/dynamic-bone
simulation, CPU/GPU vertex skinning) is completely frozen while rendering,
the Editor UI, and the independently-orbitable Scene-view camera keep
working exactly as before; Step advances the simulation by exactly one
fixed, deterministic 1/60s tick while paused, then re-freezes. A new,
dedicated, explicit (never singleton) `gte::Time` class is the single source
of truth for "how much simulated time actually passed this frame" — this
engine's equivalent of Unity's `Time.deltaTime`/`Time.timeScale`, and the
direct foundation a **future** frame-debugger campaign will build on top of.

## Phase-by-phase summary

### PHASE1 — Core `Time` class and `EngineContext`

Added a brand-new, self-contained, Tier-1-testable `src/Core/` module —
`gte::Time` (`Advance()`/`DeltaTime()`/`UnscaledDeltaTime()`/`IsPaused()`/
`IsFrozenThisFrame()`/`IsSteppedThisFrame()`/`TimeSinceStartupSeconds()`/
`SimulatedTimeSeconds()`/`FrameCount()`) and `gte::EngineContext` (a minimal
aggregate holding exactly `Time time;`) — with **zero call sites into the
rest of the engine yet**. `tests/Core/TimeTests.cpp` (9 cases) exhaustively
covers `Advance()`'s exact freeze/step/resume-clamp arithmetic in isolation.
Verified with a scoped `GreatTamanaEngineTests` build and a
`--gtest_filter=*TimeTest*` run: **9/9 passed**.

### PHASE2 — `Game::Update()` signature + internal freeze gating

Refactored `Game::Update()` to take `const EngineContext&` instead of a raw
`double deltaSeconds`, added the internal freeze branch (skip
`AnimationSystem::EvaluatePoses()`/`PhysicsSystem::Update()`/
`AnimationSystem::SkinAndUpload()` entirely when
`engineContext.time.IsFrozenThisFrame()`, calling the new
`AnimationSystem::ClearGpuSkinningDispatchThisFrame()` instead), and wired
`Application::Run()` to own one `EngineContext` member, advancing it once
per loop iteration with a **hardcoded `isPaused = false`** (the Editor pause
toggle didn't exist yet). A pure, isolated plumbing refactor with **zero
observable behavior change**. Added
`tests/Game/GameUpdateFreezeGatingTests.cpp` (4 cases) — the real,
automated proof that a frozen `EngineContext` leaves a registered
dynamic-chain pose unchanged across many `Game::Update()` calls, an
unfrozen one visibly diverges under gravity, Step-then-refreeze composes
correctly, and GPU-skinning dispatch requests stay empty across a frozen
frame. Verified: scoped filter run **4/4 passed**; full suite **1338
passed, 1 pre-existing machine-gated skip** — identical to the baseline,
confirming zero regressions.

### PHASE3 — Editor Pause/Step state and toolbar UI

Added the actual Pause/Resume + Step toolbar to the Editor UI:
`src/Editor/PlaybackControls.h/.cpp` (`BuildPlaybackToolbar()` — a
Pause/Resume toggle button, a Step button disabled unless already paused, a
"(Paused)" indicator), two new `EditorContext` fields
(`playbackPaused`/`stepOneFrameRequested`), two new `IEditorLayer`
pure-virtual methods (`IsPlaybackPaused()`/`TryConsumeStepRequest()`)
implemented for real in `ImGuiEditorLayer` and as trivial `false` constants
in `NullEditorLayer`, and `DockLayout.cpp` wiring the toolbar directly under
the menu bar, before the dockspace. **No engine-loop behavior changed in
this phase** — `Application::Run()` was untouched. Verified: both
`GTE_ENABLE_EDITOR=ON` and `=OFF` builds succeeded; full suite **1338
passed, 1 skip** (identical to PHASE2's total, confirming no regression from
adding UI-only code); a live runtime smoke test confirmed the toolbar
renders correctly (a build-hygiene gotcha was caught and documented: a
`GreatTamanaEngineTests`-only incremental build doesn't relink the separate
`GreatTamanaEngine` app target).

### PHASE4 — Application wiring: Pause/Resume/Step becomes real

`Application::Run()` now reads the Editor's real Pause/Step toolbar state
every frame (`IEditorLayer::IsPlaybackPaused()`/`TryConsumeStepRequest()`)
and feeds it into `m_engineContext.time.Advance(...)`, replacing PHASE2's
hardcoded `false`/`false` literals — this is the phase where Pause/Resume/
Step became **real, observable engine behavior**. Verified (via manual
trace/grep, not code change) that the GPU-skinning freeze path, `Game::Render()`'s
unconditional every-frame execution, `EditorCamera`'s time-independence,
`FrameProfiler`'s always-on frame counting, `PhysicsSystem`'s own internal
accumulator state, and the Network command-bridge's unconditional drain were
all already correct with **no further code change needed** — only one small,
planned doc-comment cross-reference was added. Verified: full build
succeeded; full suite **1338 passed, 1 skip** (identical totals again); a
live runtime smoke test confirmed the toolbar's live-wired enabled/disabled
state, with the interactive "actually click Pause" check explicitly
documented as a deferred manual-verification item (no mouse-control tool
available in this environment; Locked Design Decision #6 deliberately keeps
no HTTP control over this either).

### PHASE5 — Full validation, docs, and regression safety (this phase)

Closed out the campaign: a **full clean build** for both
`GTE_ENABLE_EDITOR=ON` (`build/`, 428 steps) and `=OFF`
(`build-editor-off/`, 364 steps), both with **zero errors**; a **full
`ctest` regression pass** — **100% of 1339 tests passed** (the same single
pre-existing, machine-gated `PmxLoaderRealModelSmokeTest` skip this
repository has always had) — including every pre-existing test (zero
regressions from PHASE2's refactor or PHASE4's freeze branch), all 9
`Core/TimeTests.cpp` cases, and all 4
`Game/GameUpdateFreezeGatingTests.cpp` cases; a live runtime smoke test
confirming the Editor loads with the Pause/Step toolbar visible and
correctly enabled/disabled, stable across repeated `/get_swapchain`
captures (the interactive click-driven Pause/Step/Resume check remains an
accepted, documented manual-verification gap, per PHASE4's own precedent);
and the full documentation sweep every completed campaign in this
repository makes — a new "Time and Playback Pause" section in `AGENTS.md`,
a new `docs/conventions/time-and-playback-pause.md` full write-up (linked
from `docs/README.md`'s index), a new top-of-"Status" bullet in `README.md`,
and a new "Time and Playback Pause" / "Deferred from the `frame-debugger-1`
Pause/Time campaign" section in `TODO.md` recording every Non-Goal plus the
one deferred verification item. See `PHASE5_COMPLETION_REPORT.md` for the
full detail.

## Final architecture (as landed)

```
                     +-----------------------------+
                     |   Editor (GTE_ENABLE_EDITOR) |
                     |                               |
                     |  EditorContext:                |
                     |    bool playbackPaused         |
                     |    bool stepOneFrameRequested   |
                     |                               |
                     |  PlaybackControls.cpp:         |
                     |    "Pause"/"Resume" button      |
                     |    "Step" button (enabled only  |
                     |     while paused)                |
                     |                               |
                     |  IEditorLayer (new methods):     |
                     |    IsPlaybackPaused() -> bool     |
                     |    TryConsumeStepRequest() -> bool|
                     +---------------+---------------+
                                     | read once/frame
                                     v
        +----------------------------------------------------+
        |                Application::Run()                   |
        |                                                        |
        |  realDeltaSeconds = SDL_GetTicksNS()-derived            |
        |  paused = m_editorLayer->IsPlaybackPaused()              |
        |  stepped = paused && m_editorLayer->TryConsumeStepRequest() |
        |  m_engineContext.time.Advance(realDeltaSeconds, paused,  |
        |                                 stepped, kFixedStepSeconds)|
        |  m_game.Update(m_engineContext, inputState)   <-- ALWAYS  |
        |                                                called,   |
        |                                                every frame|
        +----------------------------------------------------+
                                     |
                                     v
        +----------------------------------------------------+
        |                      Game::Update()                   |
        |                                                        |
        |  if (!engineContext.time.IsFrozenThisFrame()) {         |
        |      AnimationSystem::EvaluatePoses(registry, dt)       |
        |      PhysicsSystem::Update(registry, dt)                |
        |      AnimationSystem::SkinAndUpload(registry)            |
        |  } else {                                                |
        |      AnimationSystem::ClearGpuSkinningDispatchThisFrame()|
        |  }                                                        |
        +----------------------------------------------------+
```

`Game::Render()` and the whole render-graph/present pipeline are untouched
and keep running every single frame regardless of pause — a frozen frame
still needs to be drawn, and a future frame debugger needs the render
pipeline to behave identically whether or not simulation advanced that
frame.

## File-change inventory (final, as actually landed)

New files:
- `src/Core/Time.h`, `src/Core/Time.cpp`
- `src/Core/EngineContext.h`
- `src/Editor/PlaybackControls.h`, `src/Editor/PlaybackControls.cpp`
- `tests/Core/TimeTests.cpp`
- `tests/Game/GameUpdateFreezeGatingTests.cpp`
- `docs/conventions/time-and-playback-pause.md`
- `task_manager/frame-debugger-1/PHASE1_COMPLETION_REPORT.md` .. `PHASE5_COMPLETION_REPORT.md`
- `task_manager/frame-debugger-1/CAMPAIGN_COMPLETION_REPORT.md` (this file)

Modified files:
- `src/Game/Game.h`, `src/Game/Game.cpp`
- `src/Game/Animation/AnimationSystem.h`
- `src/Application/Application.h`, `src/Application/Application.cpp`
- `src/Editor/EditorContext.h`
- `src/Editor/EditorLayer.h`
- `src/Editor/ImGuiEditorLayer.cpp`
- `src/Editor/NullEditorLayer.cpp`
- `src/Editor/DockLayout.cpp`
- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `AGENTS.md`, `README.md`, `TODO.md`, `docs/README.md`

## Final verification evidence (PHASE5)

- **Full clean build, `GTE_ENABLE_EDITOR=ON`**: `cmake --build build --clean-first` — 428/428 steps, zero errors.
- **Full clean build, `GTE_ENABLE_EDITOR=OFF`**: `cmake --build build-editor-off --clean-first` — 364/364 steps, zero errors.
- **Full `ctest` regression pass**: `ctest -C Debug --output-on-failure` (from `build/`) — **100% tests passed, out of 1339** (1 pre-existing machine-gated skip), 105.67 sec total.
- **Live runtime smoke test**: `GreatTamanaEngine.exe` launched, `/get_swapchain` confirmed the Editor loads with the Pause ("Pause", clickable)/Step (grayed-out, disabled while not paused) toolbar visible directly under the menu bar, a second capture confirmed frame-to-frame stability, process cleanly stopped.

## Outstanding / deferred (see `TODO.md` for the full list)

- A "Stop" button with full scene-state snapshot/revert.
- A `Time.timeScale` slider (slow-motion/fast-forward).
- HTTP/network endpoints to control Play/Pause/Step remotely.
- A keyboard shortcut for Play/Pause.
- The actual future frame-debugger feature (draw-call/render-pass stepping,
  a Render Graph pass-by-pass inspector) this campaign's `Time`/
  `EngineContext` groundwork exists to support.
- Interactive click-driven Pause/Step/Resume verification via automation
  (no mouse-control tool available in this environment) — the underlying
  freeze/step/resume logic is fully proven by
  `tests/Core/TimeTests.cpp`/`tests/Game/GameUpdateFreezeGatingTests.cpp`
  regardless.

## Conclusion

All five phases of `frame-debugger-1` landed exactly per
`PHASE0_MASTER_STRATEGY.md`'s plan, with no unresolved deviations. The
engine now has a genuine, Unity-style Editor Pause/Resume/Step control,
backed by a dedicated, explicit, Tier-1-tested `gte::Time`/
`gte::EngineContext` foundation ready for a future frame-debugger campaign
to build on, verified by a full clean build (both Editor configurations), a
full `ctest` regression pass (100% of 1339 tests), and a live runtime smoke
test.
