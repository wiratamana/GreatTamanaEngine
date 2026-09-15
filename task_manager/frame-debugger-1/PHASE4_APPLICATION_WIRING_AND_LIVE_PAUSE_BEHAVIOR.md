# PHASE4 — Application wiring: make Pause/Resume/Step actually freeze the game

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1`, `PHASE2`, and
`PHASE3` all already having landed.

## Step 1: The Goal

This is the phase where Pause/Resume/Step becomes **real, observable
behavior**. `Application::Run()` reads the Editor's actual toolbar state
every frame (`IEditorLayer::IsPlaybackPaused()`/`TryConsumeStepRequest()` —
added in PHASE3) and feeds it into `m_engineContext.time.Advance(...)`
(instead of PHASE2's hardcoded `false`/`false`), which is what actually
makes `Game::Update()`'s freeze branch (added in PHASE2) engage.

After this phase: clicking "Pause" freezes all animation/physics/skinning
on the very next frame (one frame of latency — see the ordering note
below); the frozen scene keeps rendering and the Scene-view camera keeps
being navigable; clicking "Step" advances the simulation by exactly one
fixed 1/60s tick and then re-freezes; clicking "Resume" continues normally,
with the very next frame using a clamped, ordinary-sized delta rather than
replaying the entire real-world time spent paused.

## Step 2: The Situation

- `Application::Run()`'s per-frame order (from top to bottom, as of
  PHASE2) is: poll SDL events → drain one pending network engine command →
  compute `deltaSeconds` → **(PHASE2's hardcoded
  `m_engineContext.time.Advance(deltaSeconds, false, false, kFixedStepSeconds)`)**
  → `m_editorLayer->NewFrame()` → drain one pending network Editor-UI
  command → `m_renderer.BeginFrame()` → `m_game.Update(m_engineContext,
  inputState)` → render Game/Scene views → `m_editorLayer->BuildUI(...)`
  (this is where `PlaybackControls.cpp`'s buttons actually run and mutate
  `EditorContext::playbackPaused`/`stepOneFrameRequested` **for THIS
  frame**) → Present → ...
- Because `m_engineContext.time.Advance(...)` runs **before**
  `m_editorLayer->NewFrame()`/`BuildUI()` in the very same iteration, the
  pause/step state it reads at the top of any given frame is necessarily
  whatever was left over from the **previous** frame's `BuildUI()` call —
  a one-frame lag between clicking the button and the freeze actually
  engaging. This is exactly the same acceptable lag pattern already
  documented and accepted elsewhere in this exact file (e.g.
  `GameViewTarget()`'s own "one frame of lag... imperceptible in practice"
  reasoning) — no different handling is needed or expected here.
- `IEditorLayer::TryConsumeStepRequest()` (PHASE3) is **non-const** and
  mutates `EditorContext::stepOneFrameRequested` — call it carefully, only
  once per frame, and combine its result with `IsPlaybackPaused()`
  correctly (see 3.1 below for the exact, deliberately defensive
  combination).
- `kFixedStepSeconds` (a `constexpr double = 1.0/60.0`) was already added
  in PHASE2's anonymous namespace in `Application.cpp` — reuse it, don't
  redefine it.

## Step 3: The Plan

### 3.1 `src/Application/Application.cpp` — `Run()` change

Replace PHASE2's:

```cpp
m_engineContext.time.Advance(deltaSeconds, /*isPaused=*/false, /*isSteppedThisFrame=*/false, kFixedStepSeconds);
```

with:

```cpp
// frame-debugger-1 campaign, PHASE4 - read the Editor's own Pause/Step
// toolbar state (see PHASE3's IEditorLayer::IsPlaybackPaused()/
// TryConsumeStepRequest()) and drive EngineContext::Time with it for
// real. This reflects whatever the user last clicked as of the END of
// last frame's BuildUI() call - the same one-frame-of-lag every other
// Editor<->engine feedback loop in this file already accepts (see e.g.
// GameViewTarget()'s own doc comment). Always false/false for a release
// build (NullEditorLayer - see PHASE3), so Application behaves exactly
// like before this whole campaign whenever GTE_ENABLE_EDITOR is OFF.
const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
// Deliberately called EVERY frame, unconditionally (never short-circuited
// by `playbackPaused &&`) so a stray/stale pending step request can never
// linger un-cleared even in an edge case the toolbar's own "Step is
// disabled while not paused" UI guard wasn't supposed to allow in the
// first place - see IEditorLayer::TryConsumeStepRequest()'s own doc
// comment.
const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
const bool steppedThisFrame = playbackPaused && stepRequestedRaw;
m_engineContext.time.Advance(deltaSeconds, playbackPaused, steppedThisFrame, kFixedStepSeconds);
```

No other line in `Run()` changes in this phase — the
`m_game.Update(m_engineContext, inputState);` call site from PHASE2 is
already correct and untouched.

### 3.2 Verify the GPU-skinning freeze path end-to-end

This is a **verification-and-documentation** step, not expected to require
further code changes if PHASE2/PHASE3 were implemented as specified — but
must be explicitly checked, not assumed:

1. Confirm `Game::Update()`'s frozen branch (PHASE2) really does run on a
   frame where `IsFrozenThisFrame()` is true, and that it calls
   `m_animationSystem.ClearGpuSkinningDispatchThisFrame()` before
   `src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()` reads
   `Game::CollectGpuSkinningDispatchRequests()` later the same frame (it
   already must, since `m_game.Update()` runs strictly before the offscreen
   render-graph build lambda that calls `AddGpuSkinningPasses()` — but
   trace it once to be sure no reordering slipped in). Note this is now
   ALSO backed by an automated regression test, not just a manual trace:
   PHASE2's own `tests/Game/GameUpdateFreezeGatingTests.cpp` already calls
   `Game::Update()` directly with a frozen `EngineContext` and asserts
   `Game::CollectGpuSkinningDispatchRequests()` comes back empty — this
   step's own manual trace is a cheap, additional double-check on the
   REAL, wired-up `Application::Run()` call site specifically (which that
   test does not exercise — it calls `Game::Update()` directly, never
   through `Application::Run()`), not the only proof this behavior is
   correct.
2. Add a short cross-reference doc comment to
   `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`'s existing
   doc comment (`src/Game/Animation/AnimationSystem.h`) noting that a
   frozen frame (per `Game::Update()`) always reports this list as empty,
   for future readers who land on this method wondering why GPU skinning
   dispatch requests can drop to zero mid-session with the model still
   fully loaded/rigged.

### 3.3 Confirm no other code path needs a pause-awareness change

Explicitly verify (read the code, don't just assume) that none of the
following need any change for correct "freeze everything" behavior — and
if any of them DO need one, treat that as a real finding for this phase
to fix, not something to defer:

- `Game::Render()` / `RenderSystem::Draw()` — must keep rendering every
  frame regardless of pause (this is intentional — see PHASE0's
  architecture diagram). No change expected.
- `EditorCamera`/`Panels/ScenePanel.cpp` — must stay fully navigable during
  pause (confirmed in PHASE0's own investigation: driven by raw mouse
  deltas, not `Time` at all). No change expected.
- `Profiling::FrameProfiler::Instance().BeginFrame()/EndFrame()` — keeps
  counting frames/CPU scope timings every frame regardless of pause
  (consistent with `Time::FrameCount()`'s own "always increments" contract
  from PHASE1). No change expected.
- `PhysicsSystem`'s `DynamicChainRigCache`/`FixedTimestepAccumulator` state
  — since `PhysicsSystem::Update()` is simply never called on a frozen
  frame (PHASE2), its internal accumulated leftover time correctly stays
  exactly where it was — no separate "pause-aware" logic needed inside
  `PhysicsSystem` itself.
- `Network`/`EngineCommandBridge` command draining (e.g.
  `instantiate_primitive`/`delete_entity` via the HTTP API) — per PHASE0's
  Locked Design Decision #6, this campaign does not add pause-awareness to
  the network layer at all; a network-issued command still executes
  immediately regardless of pause state, exactly as it always has. Confirm
  this is still true (it should be, since nothing in this campaign touches
  `EngineCommandDispatch.cpp`) and explicitly note it as a known, accepted
  behavior (an AI-agent/network caller can still spawn/move/delete entities
  while the game is paused) rather than a gap — this is correct, not a bug.

### 3.4 Compile/behavior check for this phase

1. `cmake --build build` — must succeed.
2. Run the full existing test suite once — must show zero new failures.
3. **Live runtime smoke test** (the first point in this whole campaign
   where the pause behavior can actually be observed):
   - `run_app_background` the built `GreatTamanaEngine.exe`.
   - Use `gte_send_request` (`/get_swapchain` or `/get_game_view`) to
     confirm the Editor is up and the new toolbar is visible.
   - Spawn a primitive and/or load a test scene with something visibly
     animating (if a convenient one already exists in the repo/test
     assets) so there is something to observe freezing. If clicking the
     actual ImGui button isn't automatable from this tool set (no mouse-
     control tool is available), this specific interactive check is
     **best-effort** here and can be deferred to a human/manual pass in
     PHASE5 — do not block this phase's completion on it; a successful
     build + full test pass + confirmed-visible toolbar is sufficient
     evidence for THIS phase. PHASE5 owns the final, most thorough
     verification pass.
   - `stop_app_background` the process when done.

### 3.5 Report

Write `PHASE4_COMPLETION_REPORT.md` covering: the exact `Run()` diff, the
3.2/3.3 verification findings (explicitly state "confirmed, no change
needed" for each bullet, or describe what had to change if something did),
the test run result, and whatever runtime smoke-test evidence was
gathered. `git commit` `src/Application/Application.cpp` and (if 3.2's doc
comment was added) `src/Game/Animation/AnimationSystem.h`, plus the report.
