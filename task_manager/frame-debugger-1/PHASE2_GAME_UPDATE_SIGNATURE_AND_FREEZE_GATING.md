# PHASE2 — `Game::Update()` takes `EngineContext` + internal freeze gating

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1` already having
landed (`gte::Time`/`gte::EngineContext` must already exist and compile).

## Step 1: The Goal

Refactor `Game::Update()` to accept the new `const EngineContext&` instead
of a raw `double deltaSeconds`, and make it internally skip
Animation/Physics/skinning work whenever `EngineContext::time.IsFrozenThisFrame()`
is true. Wire `Application::Run()` to construct/advance one
`EngineContext` member and pass it through.

**Critically, this phase changes ZERO observable behavior.** The Editor
pause toggle does not exist yet (that's PHASE3), so `Application::Run()`
here hardcodes `isPaused = false` when calling `Time::Advance()` — the
engine keeps running exactly as it always has. This phase is a pure,
isolated plumbing refactor, deliberately separated from the real pause
feature (PHASE4) so that if something breaks, it's obvious which phase
introduced it.

## Step 2: The Situation

- `Game::Update(double deltaSeconds, const InputState& input)`
  (`src/Game/Game.h`, declared; `src/Game/Game.cpp`, defined) currently
  calls, in order: `m_animationSystem.EvaluatePoses(m_registry,
  deltaSeconds)`, `m_physicsSystem.Update(m_registry, deltaSeconds)`,
  `m_animationSystem.SkinAndUpload(m_registry)`.
- `Application::Run()` (`src/Application/Application.cpp`) computes
  `deltaSeconds` from `SDL_GetTicksNS()` and calls
  `m_game.Update(deltaSeconds, inputState);` once per loop iteration.
- **Confirmed via repo-wide search: no EXISTING test file anywhere calls
  `Game::Update()` directly** — every existing test-side `.Update(...)`
  call is against `PhysicsSystem::Update()` or `EditorCamera::Update()`,
  neither of which changes in this phase. This means the SIGNATURE change
  itself requires zero edits to any pre-existing test file — but this
  phase still adds one brand-new test file of its own (see 3.6 below),
  `tests/Game/GameUpdateFreezeGatingTests.cpp`, which calls
  `Game::Update()` directly for the first time in this codebase, to
  actually prove the new freeze-gating behavior works (not just that the
  refactor didn't break anything else).
- `AnimationSystem` (`src/Game/Animation/AnimationSystem.h/.cpp`) owns
  `m_gpuModelsNeedingDispatchThisFrame` (a `std::vector<std::string>`),
  rebuilt every time `SkinAndUpload()` runs, and read back by
  `CollectModelsNeedingGpuSkinningThisFrame()` (called later the same
  frame by `src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()`).
  If `SkinAndUpload()` is simply never called on a frozen frame, this
  vector keeps whatever it held from the last frame it DID run — which
  would incorrectly keep re-dispatching a GPU skinning compute pass for a
  pose that isn't changing. This phase adds the fix: a new method that
  clears it explicitly on a frozen frame.

## Step 3: The Plan

### 3.1 `src/Game/Game.h` changes

Add an include near the top (alongside the existing `#include "../Event/Event.h"`
etc.):

```cpp
#include "../Core/EngineContext.h"
```

Change the `Update()` declaration from:

```cpp
void Update(double deltaSeconds, const InputState& input);
```

to:

```cpp
// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE2_GAME_UPDATE_SIGNATURE_AND_FREEZE_GATING.md) - takes the shared,
// Application-owned EngineContext instead of a raw deltaSeconds, so this
// method (and everything it calls) has access to the full Time contract
// (DeltaTime()/IsFrozenThisFrame()/...) rather than just one already-
// resolved number. Called EVERY frame regardless of pause state (see
// PHASE0's own Locked Design Decision #2) - the freeze itself is decided
// INSIDE this method's own body, not by the caller skipping the call.
void Update(const EngineContext& engineContext, const InputState& input);
```

Update the doc comment above `Update()` to mention the new freeze
behavior (see 3.2 below for the exact wording to mirror).

### 3.2 `src/Game/Game.cpp` changes

Replace the existing body:

```cpp
void Game::Update(double deltaSeconds, const InputState& /*input*/)
{
    GTE_PROFILE_SCOPE("Game::Update");

    // Game/simulation logic goes here. Poll `input` for continuous state,
    // e.g. `if (input.IsKeyDown(KeyCode::W)) { ... }` for held-key movement.

    // Phase 3 (task_manager/verlet-integration-1/...) - ...
    m_animationSystem.EvaluatePoses(m_registry, deltaSeconds);
    m_physicsSystem.Update(m_registry, deltaSeconds);
    m_animationSystem.SkinAndUpload(m_registry);
}
```

with:

```cpp
void Game::Update(const EngineContext& engineContext, const InputState& /*input*/)
{
    GTE_PROFILE_SCOPE("Game::Update");

    // Game/simulation logic goes here. Poll `input` for continuous state,
    // e.g. `if (input.IsKeyDown(KeyCode::W)) { ... }` for held-key movement.

    const Time& time = engineContext.time;

    // frame-debugger-1 campaign - "freeze everything" (task_manager/
    // frame-debugger-1/PHASE0_MASTER_STRATEGY.md). IsFrozenThisFrame() is
    // true only while paused and NOT honoring a Step request - false on
    // every normal frame AND on a Step frame (which DOES need one real
    // simulation tick - see Time.h). Skipping these three calls entirely
    // (rather than feeding them time.DeltaTime() == 0.0) is a deliberate
    // choice: it is cheaper (no wasted IK-solve/vertex-pack/skin-upload
    // work for output that provably cannot have changed) AND does not
    // depend on every one of these three systems' own degenerate-zero-
    // delta handling staying correct forever - see PHASE0's Locked Design
    // Decision #2.
    if (!time.IsFrozenThisFrame()) {
        // Phase 3 (task_manager/verlet-integration-1/
        // PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md, v3/v4) - three
        // genuinely independent stages, communicating ONLY through the
        // ResolvedAnimationPose ECS component (see that component's own doc
        // comment): AnimationSystem samples/IK-solves/append-inherits and
        // writes a fresh pose; PhysicsSystem optionally overwrites individual
        // physics-driven bones in that SAME pose; AnimationSystem then skins and
        // uploads whatever the pose currently holds, regardless of which of the
        // two touched it last.
        m_animationSystem.EvaluatePoses(m_registry, time.DeltaTime());
        m_physicsSystem.Update(m_registry, time.DeltaTime());
        m_animationSystem.SkinAndUpload(m_registry);
    } else {
        // Nothing simulated this frame - make sure no STALE GPU-skinning
        // compute dispatch request lingers from the last frame that
        // actually ran SkinAndUpload() above (which is what normally
        // rebuilds this list every call) - see
        // AnimationSystem::ClearGpuSkinningDispatchThisFrame()'s own doc
        // comment for why this is needed.
        m_animationSystem.ClearGpuSkinningDispatchThisFrame();
    }
}
```

`Update()` itself currently has NO doc comment of its own in `Game.h` (only
`OnEvent()`, immediately above it, has one — and that comment only
mentions `Update()` in passing, e.g. "before Update() runs for that
frame"/"the InputState passed to Update() instead" — it never actually
references `deltaSeconds` at all, so it needs no wording fix). Instead, add
a NEW, brief doc comment directly above the `Update()` declaration itself —
mirroring the `// frame-debugger-1 campaign ...` comment already given for
the declaration in 3.1's code block above — so a future reader lands on an
explanation of the freeze contract right at the declaration, not only
inside `Game.cpp`'s own body comments.

### 3.3 `src/Game/Animation/AnimationSystem.h` — new method

Add, right next to the existing `CollectModelsNeedingGpuSkinningThisFrame()`
declaration:

```cpp
// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE2_GAME_UPDATE_SIGNATURE_AND_FREEZE_GATING.md) - called by
// Game::Update() INSTEAD OF SkinAndUpload() on a frame where simulation is
// frozen (paused, not stepping - see Time::IsFrozenThisFrame()). Clears
// m_gpuModelsNeedingDispatchThisFrame (normally rebuilt from scratch every
// SkinAndUpload() call) so CollectModelsNeedingGpuSkinningThisFrame() -
// read later THIS SAME FRAME by src/Application/RenderPasses.cpp's
// AddGpuSkinningPasses() - correctly reports "nothing needs a GPU skinning
// dispatch this frame" instead of silently re-reporting whatever the LAST
// frame that actually ran SkinAndUpload() happened to leave behind. A
// trivial, inline, always-safe no-op to call even in CpuJobSystem mode
// (where this vector is already always empty).
void ClearGpuSkinningDispatchThisFrame() noexcept { m_gpuModelsNeedingDispatchThisFrame.clear(); }
```

No `.cpp` change needed — this is trivial enough to stay `inline` in the
header, matching this class's existing convention for other one-line
forwarding methods (e.g. `RegisterSkinnedMesh()` just above it).

### 3.4 `src/Application/Application.h` — new member

Add `#include "../Core/EngineContext.h"` near the top, and a new member,
placed right next to `Game m_game;` (after it, since it's read by
`m_game.Update()` and conceptually belongs to the same "per-frame gameplay
state" group — no construction-order dependency either way, since
`EngineContext`/`Time` are default-constructible with no reference to
anything else):

```cpp
Game m_game;

// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE0_MASTER_STRATEGY.md) - the ONE EngineContext instance for the
// whole process, advanced exactly once per Run() loop iteration
// (m_engineContext.time.Advance(...)) and passed by const reference into
// Game::Update(). See EngineContext.h's own doc comment for why this
// stays deliberately minimal (just `time` for now).
EngineContext m_engineContext;
```

### 3.5 `src/Application/Application.cpp` — `Run()` wiring

In the anonymous namespace near the top of the file (alongside
`AspectRatioOf()`/`ToProfilingGpuSampleStatus()`/etc.), add:

```cpp
// frame-debugger-1 campaign (task_manager/frame-debugger-1/
// PHASE0_MASTER_STRATEGY.md, Locked Design Decision #4) - the fixed,
// deterministic amount of simulated time a single Step (PHASE3/PHASE4)
// advances by, and also what a resume-from-pause frame is clamped to (see
// Time::Advance()'s own doc comment, Locked Design Decision #11). A plain
// 1/60s, never derived from real elapsed time.
constexpr double kFixedStepSeconds = 1.0 / 60.0;
```

In `Run()`, find the existing:

```cpp
const Uint64 nowTicksNs = SDL_GetTicksNS();
const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
lastTicksNs = nowTicksNs;
```

Immediately after it (still before `m_editorLayer->NewFrame();`), add:

```cpp
// frame-debugger-1 campaign, PHASE2 - hardcoded "never paused" for now;
// PHASE4 replaces these two literals with the Editor's real toolbar
// state (see IEditorLayer::IsPlaybackPaused()/TryConsumeStepRequest(),
// added in PHASE3). Keeping this phase's own change limited to plumbing
// only (zero observable behavior change) is deliberate - see this
// phase's own doc comment.
m_engineContext.time.Advance(deltaSeconds, /*isPaused=*/false, /*isSteppedThisFrame=*/false, kFixedStepSeconds);
```

Then find the existing call:

```cpp
m_game.Update(deltaSeconds, inputState);
```

and change it to:

```cpp
m_game.Update(m_engineContext, inputState);
```

No other line in `Application::Run()` needs to change in this phase —
`deltaSeconds` (the raw local variable) is still used elsewhere in the
function for unrelated purposes (if any) and stays exactly as-is; only the
`Game::Update()` call site itself changes.

### 3.6 New test file: `tests/Game/GameUpdateFreezeGatingTests.cpp`

The genuine, automated regression proof for this campaign's actual freeze
BEHAVIOR (not just `gte::Time`'s own isolated arithmetic, already covered
by PHASE1's `tests/Core/TimeTests.cpp`) — calls `Game::Update()` directly
with a hand-built, frozen-vs-unfrozen `EngineContext`, mirroring
`tests/Game/GameEntityCommandsTests.cpp`'s own "`Game game;` default-
constructs cleanly, no Renderer needed" precedent, and
`tests/Game/Physics/PhysicsSystemTests.cpp`'s own
`RegisteredDynamicChainVisiblyDivergesFromPureFkPoseUnderGravity` synthetic-
rig fixture (a hand-built `SkeletonData` registered via
`PhysicsSystem::RegisterDynamicChains()`/`AttachDynamicChainRigIfNeeded()` —
both reachable from a test through `Game::GetPhysicsSystem()`/
`Game::GetRegistry()`, no PMX file/GPU involved).

Build a `Game`, register a small synthetic dynamic-chain rig against it
(mirroring the cited fixture above — a root bone plus a short
`deformAfterPhysics` run, well above `DynamicChainDetectionDefaults::
minimumChainLength`), then cover at least:

1. **A frozen `EngineContext` leaves the pose byte-for-byte unchanged**:
   construct an `EngineContext`, call `engineContext.time.Advance(1.0/60.0,
   /*isPaused=*/true, /*isSteppedThisFrame=*/false, 1.0/60.0)` (matching
   production's own `kFixedStepSeconds`), snapshot the entity's
   `ResolvedAnimationPose` BEFORE calling `game.Update(engineContext,
   InputState{})`, call it several times in a row, and assert the pose
   never changes — proving `IsFrozenThisFrame()` genuinely skips
   `PhysicsSystem::Update()` (gravity would otherwise visibly move it,
   exactly like the cited `PhysicsSystemTests.cpp` fixture already proves
   for an unfrozen call).
2. **An unfrozen `EngineContext` visibly diverges under gravity**: the same
   fixture, `engineContext.time.Advance(1.0/60.0, false, false, 1.0/60.0)`
   instead, several `game.Update(engineContext, InputState{})` calls —
   assert the pose DOES diverge from its initial bind-pose value (same
   assertion shape as `PhysicsSystemTests.cpp`'s own cited test) — proves
   this test's own fixture is a meaningful regression guard, not
   trivially "nothing ever moves anyway".
3. **A Step frame (`isSteppedThisFrame=true` while paused) still simulates
   exactly one tick**: a paused, stepped `EngineContext` — one
   `game.Update()` call moves the pose (like case 2), then a SECOND call
   with a plain frozen (non-stepped) `EngineContext` afterward leaves it
   unchanged again (like case 1) — proving Step-then-refreeze composes
   correctly through the real `Game::Update()` entry point, not just
   `Time`'s own isolated `Advance()` semantics.
4. **`Game::CollectGpuSkinningDispatchRequests()` stays empty across a
   frozen frame**: trivially true in the default `CpuJobSystem` skinning
   mode (nothing to dispatch either way regardless of pause), but still
   asserted explicitly as a regression guard confirming
   `AnimationSystem::ClearGpuSkinningDispatchThisFrame()` is genuinely
   being called from `Game::Update()`'s frozen branch rather than silently
   dead code.

No live `Renderer`/GPU device/`VkDevice` involved anywhere in this file —
`RenderSystem`/`MeshInstantiationSystem`/`AnimationSystem` are all
default-constructible with no GPU dependency (same precedent already
established by `Game/Animation/AnimationSystemEvaluatePosesTests.cpp`'s own
file comment).

### 3.7 `tests/CMakeLists.txt` edit

Add `Game/GameUpdateFreezeGatingTests.cpp` to the `set(GTE_TEST_SOURCES ...)`
list, alongside the existing `Game/Physics/PhysicsSystemTests.cpp`/
`Game/GameEntityCommandsTests.cpp` entries — unconditional (not gated
behind `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL`), since `Game`/
`PhysicsSystem`/`AnimationSystem` have no such dependency either.

### 3.8 Compile check for this phase

1. `cmake --build build` — must succeed. Watch specifically for any other
   call site anywhere in the codebase that might call `Game::Update()`
   with the old signature (there should be exactly one — the
   `Application::Run()` site above — but grep for `.Update(` calls on a
   `Game`/`m_game` receiver to be sure before declaring this done).
2. Build and run the full existing test suite once
   (`GreatTamanaEngineTests.exe`, or `ctest`) — every PRE-EXISTING test
   should show **zero** new failures (this phase's own signature/plumbing
   change is a true no-behavior-change refactor for every call site other
   than the new test below).
3. Run the new test filter specifically, e.g.
   `GreatTamanaEngineTests.exe --gtest_filter=*GameUpdateFreezeGating*` —
   every case from 3.6 above must pass. Unlike PHASE1's `*TimeTest*` filter
   (which exercises `gte::Time` in complete isolation), this is the one
   place in the whole campaign that proves the actual freeze-gating WIRING
   inside `Game::Update()` itself is correct, not just `Time`'s own
   arithmetic.

### 3.9 Report

Write `PHASE2_COMPLETION_REPORT.md` summarizing the signature change, the
new `ClearGpuSkinningDispatchThisFrame()` method, the new
`tests/Game/GameUpdateFreezeGatingTests.cpp` file and its own filtered test
run result, and the full existing suite's result (should be identical
pass/fail counts to before this phase for every PRE-EXISTING test — call
that out explicitly as proof this was a true no-behavior-change refactor
for everything except the new test itself).
`git commit` `src/Game/Game.h`, `src/Game/Game.cpp`,
`src/Game/Animation/AnimationSystem.h`, `src/Application/Application.h`,
`src/Application/Application.cpp`,
`tests/Game/GameUpdateFreezeGatingTests.cpp`, `tests/CMakeLists.txt`, and
the report.
