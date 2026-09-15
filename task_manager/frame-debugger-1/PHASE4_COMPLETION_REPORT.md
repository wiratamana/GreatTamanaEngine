# PHASE4 — Completion Report: Application wiring — Pause/Resume/Step becomes real

Phase file: `PHASE4_APPLICATION_WIRING_AND_LIVE_PAUSE_BEHAVIOR.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`

## Summary

Implemented PHASE4 exactly per its "Step 3: The Plan" — `Application::Run()`
now reads the Editor's real Pause/Step toolbar state every frame
(`IEditorLayer::IsPlaybackPaused()`/`TryConsumeStepRequest()`, added in
PHASE3) and feeds it into `m_engineContext.time.Advance(...)`, replacing
PHASE2's hardcoded `false`/`false` literals. This is the phase where
Pause/Resume/Step becomes real, observable engine behavior: `Game::Update()`'s
freeze branch (PHASE2) now genuinely engages whenever the user has paused
via the toolbar (PHASE3), and disengages for exactly one fixed 1/60s tick
when Step is clicked.

## 3.1 — `src/Application/Application.cpp` `Run()` diff

Replaced (immediately after the existing `deltaSeconds` computation, still
before `m_editorLayer->NewFrame();`):

```cpp
// frame-debugger-1 campaign, PHASE2 - hardcoded "never paused" for now;
// PHASE4 replaces these two literals with the Editor's real toolbar
// state (see IEditorLayer::IsPlaybackPaused()/TryConsumeStepRequest(),
// added in PHASE3). Keeping this phase's own change limited to plumbing
// only (zero observable behavior change) is deliberate - see this
// phase's own doc comment.
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

Copied verbatim from the phase document's own 3.1 section — no deviation.
No other line in `Run()` changed; `m_game.Update(m_engineContext, inputState);`
(from PHASE2) is untouched and already correct.

## 3.2 — GPU-skinning freeze path verification

1. **Ordering trace, confirmed by direct grep of `Application.cpp`**:
   `m_game.Update(m_engineContext, inputState);` is at line 362 (well before
   the offscreen render-graph `build` lambda's `AddGpuSkinningPasses(b,
   m_game, m_renderer);` call at line 426, and the alternate call site at
   line 834). `Game::Update()`'s frozen branch (PHASE2) calls
   `m_animationSystem.ClearGpuSkinningDispatchThisFrame()` before either of
   those `AddGpuSkinningPasses()` call sites run later the same frame — no
   reordering has slipped in since PHASE2. This is now backed by BOTH the
   automated regression test PHASE2 already added
   (`tests/Game/GameUpdateFreezeGatingTests.cpp`'s
   `GpuSkinningDispatchRequestsStayEmptyAcrossAFrozenFrame`, which calls
   `Game::Update()` directly) AND this manual trace of the real, wired-up
   `Application::Run()` call site specifically — confirmed, no code change
   needed.
2. **Doc comment cross-reference added** to
   `AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()`'s existing
   doc comment (`src/Game/Animation/AnimationSystem.h`), noting that this
   list is also empty on any frame frozen by the frame-debugger-1
   campaign's Pause/Step feature (via `Game::Update()`'s freeze branch
   calling `ClearGpuSkinningDispatchThisFrame()` instead of
   `SkinAndUpload()`), so a future reader landing on this method wondering
   why the dispatch-request list can drop to zero mid-session with the
   model still fully loaded/rigged has an immediate answer.

## 3.3 — No other pause-awareness change needed (verified, not assumed)

- **`Game::Render()` / `RenderSystem::Draw()`** — confirmed untouched; keeps
  rendering every frame regardless of pause, exactly as PHASE0's
  architecture diagram requires. No change needed.
- **`EditorCamera`/`Panels/ScenePanel.cpp`** — confirmed via grep:
  `EditorCamera::Update(Vec2 mouseDelta, float scrollDelta, bool
  middleMouseDown, bool rightMouseDown)` takes only raw mouse-pixel deltas,
  never a `Time`/delta value of any kind — structurally unaffected by
  anything this campaign does. No change needed.
- **`Profiling::FrameProfiler::Instance().BeginFrame()/EndFrame()`** —
  confirmed via grep: called unconditionally every frame in
  `Application.cpp` (`BeginFrame()` near the top of `Run()`, `EndFrame()`
  at the very end), with no pause-awareness anywhere in between — keeps
  counting frames/CPU scope timings every frame regardless of pause,
  consistent with `Time::FrameCount()`'s own "always increments" contract
  from PHASE1. No change needed.
- **`PhysicsSystem`'s `DynamicChainRigCache`/`FixedTimestepAccumulator`
  state** — since `PhysicsSystem::Update()` is simply never called on a
  frozen frame (PHASE2's freeze branch), its internal accumulated leftover
  time correctly stays exactly where it was; no separate pause-aware logic
  is needed inside `PhysicsSystem` itself, and none was added. No change
  needed.
- **`Network`/`EngineCommandBridge` command draining** — confirmed
  unchanged: `EngineCommandDispatch.cpp` was not touched anywhere in this
  campaign, and `Application::Run()`'s existing
  `m_commandBridge.TryPeekPendingCommandRequest()` drain still runs
  unconditionally, before the pause state is even read. Per PHASE0's
  Locked Design Decision #6, this is correct, accepted behavior (not a
  gap): an AI-agent/network caller (`instantiate_primitive`/
  `delete_entity`/`set_entity_trs`/`instantiate_light`) can still spawn,
  move, or delete entities while the game is paused, exactly as it always
  could. No change needed.

## Compile/behavior check performed

1. **`cmake --build build`** (working directory
   `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded with
   zero errors/warnings. Rebuilt `gte_core` (`Application.cpp`,
   `AnimationSystem.cpp` picked up the doc-comment-only header touch, plus
   several already-stale objects from the working tree), relinked both
   `GreatTamanaEngine.exe` and `tests\GreatTamanaEngineTests.exe` — both
   app and test targets were rebuilt in the same invocation this time (no
   stale-exe gotcha like PHASE3 hit).
2. **Full existing test suite** (`build\tests\GreatTamanaEngineTests.exe`,
   no filter): **`[  PASSED  ] 1338 tests.` / `[  SKIPPED ] 1 test`**
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, the
   same pre-existing machine-gated skip this repo has always had). **Zero
   new failures** — identical pass/skip counts to PHASE2/PHASE3's own
   reported totals, confirming this phase's `Run()` wiring change is a
   correct, non-regressing behavior change (the four
   `GameUpdateFreezeGatingTest` cases from PHASE2 still pass unchanged,
   since they call `Game::Update()` directly and were never exercising
   `Application::Run()`'s own wiring in the first place).
3. **Live runtime smoke test**:
   - Launched `build\GreatTamanaEngine.exe` in the background
     (`run_app_background`, PID 5076).
   - `gte_send_request('/get_swapchain')` confirmed the Editor is up and
     the "Pause"/"Step" toolbar is visible directly under the "File" menu:
     "Pause" renders as a normal clickable button, "Step" renders visibly
     grayed-out/disabled (playback is not paused) — matching
     `PlaybackControls.cpp`'s documented enabled/disabled contract exactly.
     The Hierarchy/Scene/Game/Project panels are all present and rendering
     a live scene (`TestScene.gtscene` in the Project panel, a camera
     entity in the Hierarchy, a sky-gradient Scene/Game view).
   - Per the phase document's own explicit allowance: no mouse-control tool
     is available in this tool set to actually click the ImGui "Pause"
     button (`EditorUiCommandBridge`'s only command today is
     `ActivateTab`, added by `network-impl-7` — there is no HTTP endpoint
     for toggling `EditorContext::playbackPaused`, and Locked Design
     Decision #6 deliberately keeps it that way), so the interactive
     "click Pause, confirm the scene visibly freezes" check could not be
     automated from here. Per the phase document's own words, this is
     **best-effort** and explicitly deferred to a human/manual pass —
     the successful build, full test pass, and confirmed-visible toolbar
     above are sufficient evidence for this phase's own completion.
   - Cleanly stopped the process via `stop_app_background(pid: 5076)`.

## Notes for the next phase (PHASE5)

- `Application::Run()`'s pause/step wiring is fully real end-to-end now:
  clicking "Pause" in the Editor freezes Animation/Physics/skinning on the
  very next frame; "Step" advances exactly one fixed 1/60s tick then
  re-freezes; "Resume" continues normally with a clamped, ordinary-sized
  delta (never a giant catch-up burst) thanks to PHASE1's
  `Time::Advance()` "was paused last call" latch.
- The one interactive gap this phase could not close itself (actually
  clicking the toolbar button and visually confirming the freeze) is
  explicitly flagged above as a manual/human verification item for
  PHASE5's own, more thorough validation pass — not a defect in this
  phase's own implementation, which is otherwise fully verified by the
  automated `GameUpdateFreezeGatingTest` suite (PHASE2) exercising the
  exact same freeze/step/resume logic this phase now wires up live.
- No surprises or deviations from the phase document's plan were found —
  every 3.2/3.3 verification bullet was "confirmed, no change needed"
  except the one explicitly-planned doc-comment addition in 3.2.2, which
  was made exactly as specified.

## Git

`src/Application/Application.cpp`, `src/Game/Animation/AnimationSystem.h`,
plus this report, were staged and committed together with a commit message
referencing PHASE4.
