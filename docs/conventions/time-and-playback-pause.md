# Time and Playback Pause

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The `frame-debugger-1` campaign
(`task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`, five phases) gave
the engine its first genuine, Unity-style **Pause/Resume/Step** capability,
built on a brand-new, dedicated, explicit (never singleton) `gte::Time` class
plus a minimal `gte::EngineContext` aggregate. Follow these rules whenever
touching time-keeping, the pause/step toolbar, or anything that reads
`EngineContext`/`Time`:

- **`src/Core/` is the engine's first "bootstrap/loop state" module** -
  lower-level than `Application`/`Game`/`Editor` (see
  [Coding Guidelines](../../AGENTS.md#coding-guidelines)'s Clean Architecture
  rule). It holds exactly two files: `Time.h/.cpp` and `EngineContext.h`.
  Deliberately not folded into `src/Math/` or any existing module - it is
  conceptually neither math, nor ECS, nor any existing subsystem.
- **`gte::Time` (`src/Core/Time.h/.cpp`) is an explicit, dependency-injected
  object - never a singleton/static accessor.** Exactly one instance is
  owned by `Application` (`Application::m_engineContext.time`), advanced
  exactly once per `Application::Run()` loop iteration via `Advance()`, and
  threaded down by `const&` through `EngineContext` into whatever engine-core
  code needs it (`Game::Update()`, as of PHASE2). `Advance()` is the ONLY way
  its state ever changes; every other method is a read-only accessor for
  whatever the last `Advance()` call computed. Its full API surface:
  - `Advance(realDeltaSeconds, isPaused, isSteppedThisFrame,
    fixedStepSeconds)` - see the next four bullets for the four parameters'
    contracts.
  - `DeltaTime()` - the delta simulation/gameplay code should actually use
    (Unity's own `Time.deltaTime` equivalent): `realDeltaSeconds` while
    running normally, exactly `fixedStepSeconds` on a Step frame OR on the
    one frame immediately after resuming from a pause (see the
    "resume-clamp" bullet below), or exactly `0.0` while frozen (paused, not
    stepping).
  - `UnscaledDeltaTime()` - always the real, wall-clock elapsed seconds since
    the last `Advance()` call, REGARDLESS of pause (Unity's own
    `Time.unscaledDeltaTime` equivalent) - reserved for any future
    pause-independent per-frame logic; nothing in this campaign currently
    reads it (the Editor's Scene camera is driven by raw per-pixel mouse
    deltas, not a time base at all - see `EditorCamera::Update()`).
  - `IsPaused()` - this frame's raw pause/resume state, as decided by
    whoever owns the Play/Pause UI.
  - `IsFrozenThisFrame()` - **the one flag `Game::Update()` actually branches
    on.** True only on a frame where NO simulation work should run at all:
    paused AND not currently honoring a Step request. False on every normal
    running frame AND on a Step frame (a Step frame DOES advance the
    simulation, by exactly `fixedStepSeconds`).
  - `IsSteppedThisFrame()` - true only on the exact frame a Step request is
    being honored (always implies `IsPaused() == true`).
  - `TimeSinceStartupSeconds()` - total REAL (unscaled) seconds elapsed since
    the very first `Advance()` call - never frozen by pause, monotonically
    increasing every call.
  - `SimulatedTimeSeconds()` - total SIMULATED seconds elapsed: the running
    sum of every past `DeltaTime()` value - freezes while paused (not
    stepping), and jumps forward by exactly `fixedStepSeconds` on each Step
    or resume-frame.
  - `FrameCount()` - increments by exactly 1 on every single `Advance()`
    call, regardless of pause state - a real frame still happened, even a
    frozen one.
- **`gte::EngineContext` (`src/Core/EngineContext.h`) is a deliberately
  minimal aggregate - today, exactly one field, `Time time;`.** It is a
  real, intentional extension point for later engine-wide per-frame needs
  (e.g. a future frame-debugger's "currently inspected pass" handle), NOT a
  speculative grab-bag to pre-populate now - add a new field only when a
  real need arrives, mirroring `src/Editor/EditorContext.h`'s own
  already-established "add fields only when a real need arrives" convention.
  Owned by `Application` (`Application::m_engineContext`), advanced exactly
  once per frame, and passed by `const&` into `Game::Update()` - never
  copied around casually, and never mutated by anything except
  `Application::Run()` itself.
- **`Game::Update()` is called every single frame, unconditionally, pause or
  not - this mirrors real Unity, where `Time.timeScale = 0` never stops
  `MonoBehaviour.Update()` from being called; it only makes `Time.deltaTime`
  read as `0`.** This is deliberate, forward-looking architecture for a
  **future** frame-debugger campaign (this folder's own name is the hint):
  the whole per-frame pipeline (input handling, `Game::Update()`, rendering,
  profiling, the render graph) keeps executing identically every frame
  whether or not simulation actually advanced - a future "step through one
  frame and inspect it" feature has nothing special to special-case. The
  actual freeze happens INSIDE `Game::Update()`: it reads
  `engineContext.time.IsFrozenThisFrame()` and, when true, skips
  `AnimationSystem::EvaluatePoses()`/`PhysicsSystem::Update()`/
  `AnimationSystem::SkinAndUpload()` entirely (cheaper than feeding them a
  zero delta and relying on each one's own degenerate-zero handling, though
  that would also be correct as a fallback) - calling
  `AnimationSystem::ClearGpuSkinningDispatchThisFrame()` instead, so no stale
  GPU-skinning dispatch request lingers from the last frame that actually
  ran `SkinAndUpload()` (see `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`'s
  own doc comment for the cross-reference). `Game::Render()` and the whole
  render-graph/present pipeline are UNTOUCHED and keep running every single
  frame regardless of pause - a frozen frame still needs to be drawn (the
  user is looking at it!).
- **Step is a fixed, deterministic 1/60s tick (`kFixedStepSeconds`,
  `Application.cpp`), never derived from real elapsed wall-clock time** -
  fully reproducible regardless of how long the user actually paused for or
  how fast they click. There is no `Time.timeScale` slider (slow-motion/
  fast-forward) in this campaign - Pause is binary (running or fully frozen)
  only; `Time`'s own API is written so a future `timeScale` field would be a
  small, additive change, not a redesign.
- **Resuming from an arbitrarily long pause is clamped to a single
  ordinary-sized simulation step, never a giant catch-up burst.** If the
  user pauses for (say) 30 real seconds and then resumes, the very next
  running frame simulates one `kFixedStepSeconds`-sized step, NOT 30
  seconds' worth of motion in one call (which could tunnel a fast-moving
  physics object through a thin collider, or make an animation clip jump
  forward by 30 seconds instantly). Mechanism: `Time` keeps an internal
  `m_wasPausedLastCall` latch - the first `Advance()` call after a paused
  call clamps that one frame's `DeltaTime()` to `fixedStepSeconds` instead of
  the (possibly huge) `realDeltaSeconds` actually elapsed across the whole
  pause; every subsequent frame goes back to normal real-delta behavior.
  Covered exhaustively by `tests/Core/TimeTests.cpp`'s
  `ResumeAfterLongPauseClampsExactlyOneFrameToFixedStep`.
- **Where the toggle *intent* lives vs. where the *bookkeeping* lives are two
  different things, on purpose.** The raw toggle intent
  (`playbackPaused`/`stepOneFrameRequested` booleans) lives in
  `EditorContext` (Editor-only - see
  [Editor Module Structure](editor-module-structure.md) - since only the
  Editor UI can toggle them today), while the time bookkeeping object
  (`Time`, inside `EngineContext`) lives in engine-core, owned by
  `Application`, completely independent of whether an Editor even exists.
  `Application::Run()` is the one bridge that reads the Editor-owned intent
  once per frame and feeds it into the engine-core-owned `Time` object:
  ```cpp
  const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
  // Deliberately called EVERY frame, unconditionally, so a stray/stale
  // pending step request can never linger un-cleared.
  const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
  const bool steppedThisFrame = playbackPaused && stepRequestedRaw;
  m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame, kFixedStepSeconds);
  m_game.Update(m_engineContext, inputState); // ALWAYS called, every frame
  ```
  A release build (`GTE_ENABLE_EDITOR=OFF`, `NullEditorLayer`) always reports
  `IsPlaybackPaused() == false`/`TryConsumeStepRequest() == false`, so
  `Application` behaves exactly like it did before this whole campaign
  whenever the Editor doesn't exist at all - confirmed by a full
  `GTE_ENABLE_EDITOR=OFF` clean build in PHASE5.
- **The Pause/Resume + Step toolbar** (`src/Editor/PlaybackControls.h/.cpp`,
  `BuildPlaybackToolbar(EditorContext&)`) renders as a small, fixed strip
  directly under the menu bar, always before the dockspace - a single toggle
  button whose label flips between "Pause"/"Resume" depending on
  `EditorContext::playbackPaused`, a "Step" button wrapped in
  `ImGui::BeginDisabled(!ctx.playbackPaused)` (visibly grayed-out/
  un-clickable unless already paused), and a "(Paused)" text indicator shown
  only while paused. `IEditorLayer` exposes exactly two new accessors
  (`IsPlaybackPaused() const`/`TryConsumeStepRequest()`) that
  `Application::Run()` reads - `TryConsumeStepRequest()` follows a
  read-and-clear contract (returns `true` at most once per click,
  `NullEditorLayer` always returns `false` for both). Pure ImGui rendering
  code with no independently-testable logic of its own (mirrors
  `DockLayout.cpp`'s existing "no dedicated test file" precedent) - the
  actual freeze/step/resume BEHAVIOR is proven instead by
  `tests/Game/GameUpdateFreezeGatingTests.cpp` (below), which calls
  `Game::Update()` directly with hand-built `EngineContext` values.
- **The Editor's Scene-view camera stays fully navigable during pause with
  ZERO code changes** - `EditorCamera::Update(Vec2 mouseDelta, float
  scrollDelta, bool, bool)` is driven purely by raw per-frame mouse-pixel
  deltas, never by any `Time`/deltaSeconds value at all. Do not add a
  `Time`/`EngineContext` dependency to `EditorCamera` to "respect" pause -
  it is structurally, intentionally independent of it.
- **Two dedicated regression-test files are this campaign's actual
  correctness proof - never loosen either without understanding why it
  failed:**
  - `tests/Core/TimeTests.cpp` - 9 cases exhaustively covering `Time`'s own
    isolated `Advance()` arithmetic in every combination of paused/stepped/
    resumed, entirely independent of `Game`/ECS/GPU/SDL.
  - `tests/Game/GameUpdateFreezeGatingTests.cpp` - the real proof that
    `Game::Update()` itself actually skips Animation/Physics/GPU-skinning
    work on a frozen frame (and resumes/steps correctly), built on the same
    synthetic 2-joint dynamic-chain-rig fixture shape
    `tests/Game/Physics/PhysicsSystemTests.cpp` already established. A
    frozen `EngineContext` leaves a registered dynamic-chain pose completely
    unchanged across many calls; an unfrozen one visibly diverges from pure
    forward-kinematics under gravity; a Step-then-refreeze sequence composes
    correctly; and a frozen frame leaves
    `Game::CollectGpuSkinningDispatchRequests()` empty.
- **Explicitly out of scope for this campaign** (see
  `task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md`'s own
  Non-Goals, and `TODO.md`'s "Deferred from the `frame-debugger-1`
  Pause/Time campaign" section): a "Stop" button with full scene-state
  snapshot/revert, a `Time.timeScale` slider, any HTTP/network endpoint to
  control Play/Pause/Step remotely, a keyboard shortcut for Play/Pause, and
  the actual future frame-debugger feature (draw-call/render-pass stepping,
  a Render Graph pass-by-pass inspector) this campaign's `Time`/
  `EngineContext` groundwork exists to support.
