# PHASE2 — Completion Report: `Game::Update()` takes `EngineContext` + internal freeze gating

Phase file: `PHASE2_GAME_UPDATE_SIGNATURE_AND_FREEZE_GATING.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Branch: `feature/frame-debugger-impl`

## Summary

Implemented PHASE2 exactly per its "Step 3: The Plan" — refactored
`Game::Update()` to take `const EngineContext&` instead of a raw
`double deltaSeconds`, added the internal freeze-gate branch (skip
Animation/Physics/skinning entirely when `EngineContext::time.
IsFrozenThisFrame()` is true, otherwise call them exactly as before), added
`AnimationSystem::ClearGpuSkinningDispatchThisFrame()`, and wired
`Application::Run()` to own one `EngineContext` member, advance it once per
loop iteration with a hardcoded `isPaused = false` (the Editor pause toggle
doesn't exist yet — that's PHASE3/PHASE4), and pass it through to
`Game::Update()`. Per the phase document's own explicit framing, this is a
**pure, isolated plumbing refactor with ZERO observable behavior change** for
every pre-existing call site — confirmed by the full test suite showing
identical pass/fail counts to before this phase.

## What was changed

- **`src/Game/Game.h`**
  - Added `#include "../Core/EngineContext.h"` alongside the existing
    `#include "../Event/Event.h"`/`"../Input/InputState.h"` includes.
  - Changed the `Update()` declaration from
    `void Update(double deltaSeconds, const InputState& input);` to
    `void Update(const EngineContext& engineContext, const InputState& input);`,
    with a new doc comment directly above it explaining the freeze contract
    (copied verbatim from the phase document's own wording).

- **`src/Game/Game.cpp`**
  - `Game::Update()`'s body now reads `const Time& time =
    engineContext.time;` and branches on `time.IsFrozenThisFrame()`:
    - **Not frozen** (every normal running frame AND a Step frame): calls
      `m_animationSystem.EvaluatePoses(m_registry, time.DeltaTime())`,
      `m_physicsSystem.Update(m_registry, time.DeltaTime())`,
      `m_animationSystem.SkinAndUpload(m_registry)` — exactly the same three
      calls as before, just reading `time.DeltaTime()` instead of the old raw
      parameter.
    - **Frozen** (paused, not stepping): calls
      `m_animationSystem.ClearGpuSkinningDispatchThisFrame()` instead, so no
      stale GPU-skinning dispatch request lingers from the last frame that
      actually ran `SkinAndUpload()`.

- **`src/Game/Animation/AnimationSystem.h`**
  - Added `ClearGpuSkinningDispatchThisFrame() noexcept` — a trivial,
    always-safe inline one-liner (`m_gpuModelsNeedingDispatchThisFrame.clear();`),
    placed right next to the existing
    `CollectModelsNeedingGpuSkinningThisFrame()` declaration, per the phase
    document's exact wording. No `.cpp` change needed (matches this class's
    existing convention for other one-line forwarding methods).

- **`src/Application/Application.h`**
  - Added `#include "../Core/EngineContext.h"` near the top.
  - Added a new member, `EngineContext m_engineContext;`, placed right after
    `Game m_game;` (no construction-order dependency either way, since
    `EngineContext`/`Time` are default-constructible with no reference to
    anything else).

- **`src/Application/Application.cpp`**
  - Added `constexpr double kFixedStepSeconds = 1.0 / 60.0;` to the anonymous
    namespace near the top, alongside `AspectRatioOf()`/
    `ToProfilingGpuSampleStatus()`.
  - In `Run()`, immediately after the existing `deltaSeconds` computation
    (still before `m_editorLayer->NewFrame();`), added:
    `m_engineContext.time.Advance(deltaSeconds, /*isPaused=*/false,
    /*isSteppedThisFrame=*/false, kFixedStepSeconds);` — hardcoded "never
    paused" for now, per the phase document's own explicit instruction (PHASE4
    replaces these two literals with the Editor's real toolbar state).
  - Changed the existing `m_game.Update(deltaSeconds, inputState);` call to
    `m_game.Update(m_engineContext, inputState);` — the one and only call
    site that needed to change (confirmed via a repo-wide `.Update(` grep on
    a `Game`/`m_game` receiver before declaring this done — no other call
    site exists).

## New test file

- **`tests/Game/GameUpdateFreezeGatingTests.cpp`** — the genuine, automated
  regression proof for this campaign's actual freeze BEHAVIOR (not just
  `gte::Time`'s own isolated arithmetic, already covered by PHASE1's
  `tests/Core/TimeTests.cpp`). Builds a real `Game`, registers a synthetic
  2-joint dynamic-chain rig (root → chainRoot [Static anchor] → joint1
  [Dynamic] → joint2 [Dynamic], extending along +X — the exact same fixture
  shape as `tests/Game/Physics/PhysicsSystemTests.cpp`'s own
  `RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity`) via
  `Game::GetPhysicsSystem()`/`Game::GetRegistry()`, seeds a bind-pose
  `ResolvedAnimationPose`, and enables gravity. Four `TEST()` cases:
  1. `FrozenEngineContextLeavesPoseUnchangedAcrossManyCalls` — a paused,
     non-stepped `EngineContext`, 10 `game.Update()` calls in a row, asserts
     the pose never changes (proves `IsFrozenThisFrame()` genuinely skips
     `PhysicsSystem::Update()`).
  2. `UnfrozenEngineContextVisiblyDivergesFromPureFkPoseUnderGravity` — a
     running (unpaused) `EngineContext`, 30 calls, asserts the pose DOES
     diverge from bind pose (proves the fixture is a meaningful regression
     guard, not trivially "nothing ever moves anyway").
  3. `StepFrameSimulatesOnceThenRefreezeHoldsSteady` — several
     paused+stepped calls (each one simulates, like case 2), then several
     plain paused (non-stepped) calls afterward (the pose holds exactly
     steady, like case 1) — proves Step-then-refreeze composes correctly
     through the real `Game::Update()` entry point.
  4. `GpuSkinningDispatchRequestsStayEmptyAcrossAFrozenFrame` — a plain
     default-constructed `Game`, one frozen `game.Update()` call, asserts
     `game.CollectGpuSkinningDispatchRequests()` is empty — a regression
     guard confirming `ClearGpuSkinningDispatchThisFrame()` is genuinely
     called from the frozen branch (trivially true in the default
     `CpuJobSystem` skinning mode either way, but still asserted explicitly).

  No live `Renderer`/GPU device/`VkDevice` involved anywhere — mirrors
  `tests/Game/GameEntityCommandsTests.cpp`'s "`Game game;` default-constructs
  cleanly" precedent.

## Modified build files

- **`tests/CMakeLists.txt`** — added `Game/GameUpdateFreezeGatingTests.cpp` to
  `GTE_TEST_SOURCES`, immediately after `Game/GameEntityCommandsTests.cpp`
  (unconditional — no `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` gate,
  since `Game`/`PhysicsSystem`/`AnimationSystem` have no such dependency).
  Also added a matching entry to the file's own "Test taxonomy" comment
  block, mirroring the style of every other listed test file.

## One deviation from a literal test-file copy worth calling out

The phase document's own example code sketched `CurrentPose()` as a `const`
helper method on the test fixture struct. During implementation this failed
to compile: `Game::GetRegistry()` is non-`const` (by design — see
`Game.h`'s own doc comment: "Non-const because the Inspector panel edits
component fields ... in place"), so a `const` `CurrentPose()` calling
`game.GetRegistry()` on a `const Game& this` cannot bind. Fixed by making
`GameUpdateFreezeGatingFixture::CurrentPose()` non-`const` instead — no
behavior change, purely a `const`-correctness fix required by `Game`'s own
existing, intentional API shape. No other deviation from the phase
document's plan.

## Compile/test check performed

1. **Scoped incremental build**: `cmake --build build --target
   GreatTamanaEngineTests` (working directory
   `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded with zero
   errors after the `const`-correctness fix above (`gte_core` relinked,
   `tests\GreatTamanaEngineTests.exe` rebuilt).
2. **New test filter**: `tests\GreatTamanaEngineTests.exe
   --gtest_filter=*GameUpdateFreezeGating*` (from the `build` directory) —
   result: **`[ PASSED ] 4 tests.`** (all four cases from 3.6 above, 0
   failures).
3. **Full existing suite**: `tests\GreatTamanaEngineTests.exe` (no filter) —
   result: **`[  PASSED  ] 1338 tests.` / `[  SKIPPED ] 1 test`**
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`, the
   same pre-existing machine-gated skip this repo has always had — nothing
   related to this phase). **Zero new failures** — every PRE-EXISTING test
   passed exactly as it did before this phase, confirming the signature/
   plumbing change is a true no-behavior-change refactor for everything
   except the four brand-new tests themselves. Per the phase document's own
   instruction ("Watch specifically for any other call site anywhere in the
   codebase that might call `Game::Update()` with the old signature"), a
   repo-wide grep for `.Update(` calls on a `Game`/`m_game` receiver
   confirmed exactly one call site (`Application.cpp`'s `Run()`) both before
   and after this change.
4. Per PHASE0/PHASE2's own instructions, no full clean build and no
   `ctest`-driven full regression pass beyond the scoped test binary run
   above were required in this phase — that step is reserved for PHASE5.

## Notes for the next phase (PHASE3)

- `Game::Update()`'s new `const EngineContext&` signature and internal
  freeze gate are fully wired and tested. `Application::Run()` currently
  hardcodes `isPaused = false`/`isSteppedThisFrame = false` when calling
  `m_engineContext.time.Advance()` — PHASE3 introduces the Editor toolbar
  state (`EditorContext::playbackPaused`/`stepOneFrameRequested`,
  `IEditorLayer::IsPlaybackPaused()`/`TryConsumeStepRequest()`), and PHASE4
  is what actually threads those into `Application::Run()`'s two hardcoded
  literals, making Pause/Step/Resume real.
- No surprises or deviations from the phase document's plan were found
  beyond the one `const`-correctness fix noted above, which does not affect
  any documented behavior or API contract.

## Git

All new/modified files for this phase, plus this report, were staged and
committed together with a commit message referencing PHASE2.
