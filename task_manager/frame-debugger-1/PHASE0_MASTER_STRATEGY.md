# PHASE0 — MASTER STRATEGY: Unity-style Pause/Step + a dedicated `Time` class

Campaign folder: `task_manager/frame-debugger-1/`
Branch: `feature/frame-debugger-impl`

This is the **orchestrator** document. It does not itself contain
implementation instructions — each child phase (`PHASE1`..`PHASE5`) is a
self-contained, independently implementable chunk. Read this file first,
then work the phases **in numeric order** (each one assumes the previous
ones already landed).

## Phase list

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_CORE_TIME_CLASS_AND_ENGINE_CONTEXT.md` | New, dependency-injected `gte::Time` class + `gte::EngineContext` aggregate, under a new `src/Core/` module. Pure, Tier-1-tested, touches nothing else yet. |
| 2 | `PHASE2_GAME_UPDATE_SIGNATURE_AND_FREEZE_GATING.md` | `Game::Update()` takes `const EngineContext&` instead of a raw `double deltaSeconds`; internally gates Animation/Physics/skinning behind `Time::IsFrozenThisFrame()`. Application wires a hardcoded "never paused" `EngineContext` through it — a pure refactor, zero behavior change, isolated from the real pause feature for easy bisection. |
| 3 | `PHASE3_EDITOR_PAUSE_STEP_STATE_AND_TOOLBAR_UI.md` | New Editor-only Pause/Resume + Step toolbar (`EditorContext` fields, `IEditorLayer` accessors, `PlaybackControls.h/.cpp`, `NullEditorLayer` no-ops). No engine-loop behavior change yet — this phase only makes the buttons exist and the state readable. |
| 4 | `PHASE4_APPLICATION_WIRING_AND_LIVE_PAUSE_BEHAVIOR.md` | `Application::Run()` actually reads the Editor's pause/step state every frame and drives `EngineContext::Time::Advance()` with it. This is the phase where Pause/Step/Resume becomes REAL and "freezes everything". |
| 5 | `PHASE5_VALIDATION_DOCS_AND_REGRESSION_SAFETY.md` | Full build + full `ctest` regression, a live runtime smoke test, and doc updates (`AGENTS.md`/`README.md`/`TODO.md`/`docs/`). |

---

## Step 1: The Goal (Where are we going?)

Give GreatTamanaEngine a genuine, Unity-style **Pause** capability for its
Editor:

- A **Pause/Resume** toggle button and a **Step** button in the Editor UI
  (matching the reference screenshot the user supplied — a small toolbar
  strip with playback controls).
- While paused, gameplay simulation (skeletal animation playback, IK,
  physics/dynamic-bone-chain simulation, CPU/GPU vertex skinning) is
  **completely frozen** — nothing moves, nothing re-simulates, nothing
  re-uploads GPU vertex data — while rendering, the Editor UI itself, and
  the independently-orbitable Scene-view camera all keep working exactly as
  before (you can still look around a frozen scene, exactly like Unity).
- **Step** advances the simulation by exactly one fixed, deterministic
  1/60s tick while paused, then re-freezes — useful for inspecting a scene
  one simulation tick at a time.
- A new, dedicated, explicit (never singleton) `gte::Time` class is the
  single source of truth for "how much simulated time actually passed this
  frame" — this engine's equivalent of Unity's `Time.deltaTime`/
  `Time.timeScale`, and the direct foundation a **future** frame-debugger
  campaign (this folder's own name is a hint — draw-call/render-pass
  stepping) will build on top of. That future work is explicitly **out of
  scope** for this campaign — see "Non-Goals" below — but every design
  choice here is made so it doesn't have to be undone later.

## Step 2: The Situation (Where are we now?)

Investigated directly in the current `feature/frame-debugger-impl`
checkout (see file references throughout):

- **There is no Play/Edit mode distinction at all today.** `Game::Update()`
  runs every single frame, unconditionally, forever — the engine has
  always behaved as if it were permanently "in Play mode". There is no
  Pause, no Stop, no snapshot/revert of any kind.
- **The main loop** (`src/Application/Application.cpp`, `Application::Run()`)
  computes `deltaSeconds` from `SDL_GetTicksNS()` once per frame and passes
  it straight into `m_game.Update(deltaSeconds, inputState)` — see lines
  ~290–333 (as of this writing).
- **`Game::Update(double deltaSeconds, const InputState&)`**
  (`src/Game/Game.h`/`.cpp`) calls, in this fixed order:
  `AnimationSystem::EvaluatePoses(registry, deltaSeconds)` →
  `PhysicsSystem::Update(registry, deltaSeconds)` →
  `AnimationSystem::SkinAndUpload(registry)`. All three are pure functions
  of `deltaSeconds`/the ECS registry — none of them know a pause concept
  exists.
- **`PhysicsSystem::Update()`** (`src/Game/Physics/PhysicsSystem.h/.cpp`)
  internally uses `FixedTimestepAccumulator.h`'s
  `ComputeFixedStepCount()` — an accumulator pattern that already degrades
  gracefully to "zero steps" for a zero (or very small) delta. Feeding it
  `deltaSeconds == 0` is already a safe, correct no-op.
- **`AnimationSystem::SkinAndUpload()`** (`src/Game/Animation/AnimationSystem.h/.cpp`)
  rebuilds `m_gpuModelsNeedingDispatchThisFrame` "from scratch" every time
  it runs — this is what `CollectModelsNeedingGpuSkinningThisFrame()`
  (read once per frame by `src/Application/RenderPasses.cpp`'s
  `AddGpuSkinningPasses()`, called from `Application::Run()`, **after**
  `Game::Update()` already ran) reports back as "dispatch this GPU
  skinning compute pass this frame". If `SkinAndUpload()` simply isn't
  called on a frozen frame, this vector is never touched — it still holds
  **last frame's** contents unless something explicitly clears it (see
  PHASE2 for the fix).
- **The Editor** (`src/Editor/`, gated by `GTE_ENABLE_EDITOR`) has a fixed
  dock layout (`Hierarchy`/`Inspector`/`Scene`/`Game`/`Memory`/`Profiler`/
  `Render Graph`/`Jobs`/`Atmosphere`/`Project`) built by
  `DockLayout.cpp`'s `BuildDockspaceAndMenuBar()`. There is **no toolbar
  today at all** — no Play/Pause/Step/Stop buttons of any kind exist yet.
  `EditorContext` (`src/Editor/EditorContext.h`) is the existing, plain,
  no-behavior shared-state struct every panel/dock-layout function reads
  and writes — the natural, already-established place for new UI state
  like a pause flag.
- **`IEditorLayer`** (`src/Editor/EditorLayer.h`) is the abstraction
  boundary `Application` talks to, with two implementations:
  `ImGuiEditorLayer` (real, `GTE_ENABLE_EDITOR=ON`) and `NullEditorLayer`
  (inert no-op, `GTE_ENABLE_EDITOR=OFF`). Any new "ask the Editor whether
  we're paused" capability must go through this interface, exactly like
  `WantsExit()`/`WantsCaptureMouse()` already do.
- **The Editor's Scene-view camera** (`src/Editor/EditorCamera.h`) is
  driven purely by raw per-frame mouse-pixel deltas
  (`EditorCamera::Update(Vec2 mouseDelta, float scrollDelta, bool, bool)`),
  **never** by any deltaSeconds/Time value at all. This means Scene-view
  navigation is **already, structurally, completely unaffected** by
  anything this campaign does to `Game::Update()`'s delta — no code change
  is needed to satisfy "the Scene camera stays navigable during pause".
- **No source file in this repository currently defines a `Time` class or
  an `EngineContext` struct.** Both are entirely new additions. The
  natural new home for them is a new `src/Core/` module (lower-level than
  `Application`/`Game`/`Editor` — see `AGENTS.md`'s "Clean Architecture"
  rule that lower-level code must not depend on higher-level code; `Time`/
  `EngineContext` must be includable from `Game.h` without `Game.h` ever
  depending on `Application/` or `Editor/`).
- **Every engine source file is listed explicitly** in the root
  `CMakeLists.txt`'s `target_sources(gte_core ...)` call (no globbing) —
  and mirrored in `tests/CMakeLists.txt`'s `GTE_TEST_SOURCES` list for any
  new Tier-1 test file. Both files need explicit edits in this campaign.
- **No test file anywhere calls `Game::Update()` directly** (confirmed via
  repo-wide search) — every existing `.Update(...)` call in `tests/` is
  against `PhysicsSystem::Update()` or `EditorCamera::Update()`, neither of
  which this campaign touches. This means **changing `Game::Update()`'s
  signature requires zero test-file changes** — a very low-risk refactor.
- **`Game` itself is genuinely Tier-1-testable with no live Renderer/GPU
  device at all** — confirmed by existing precedent:
  `tests/Game/GameEntityCommandsTests.cpp` default-constructs a real `Game`
  and reaches its ECS world via `GetRegistry()` with no Renderer at all, and
  `tests/Game/Physics/PhysicsSystemTests.cpp`'s own synthetic-rig fixture
  (`RegisterDynamicChains()`/`AttachDynamicChainRigIfNeeded()` against a
  hand-built `SkeletonData`, no PMX file/GPU involved) proves a registered
  dynamic bone chain visibly diverges from its pure-FK pose under gravity.
  This means the actual freeze-gating behavior this whole campaign exists
  to add — not just `gte::Time`'s own isolated arithmetic (PHASE1) — can
  and should get its own real, automated regression test calling
  `Game::Update()` directly with a hand-built, frozen-vs-unfrozen
  `EngineContext`, instead of relying solely on manual/visual verification
  (PHASE4/PHASE5) for the one behavior this campaign is actually about. See
  PHASE2's own new `tests/Game/GameUpdateFreezeGatingTests.cpp`.

## Step 3: The Plan (detailed strategy)

### 3.1 Architecture at a glance

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

`Game::Render()` and the whole render-graph/present pipeline are
**untouched and keep running every single frame regardless of pause** —
this is deliberate (see Locked Design Decision #2): a frozen frame still
needs to be drawn (the user is looking at it!), and a future frame debugger
needs the render pipeline to behave identically whether or not simulation
advanced that frame.

### 3.2 New module: `src/Core/`

This campaign introduces the engine's first `src/Core/` folder — a new,
lower-level-than-everything-else module (see `AGENTS.md`'s Clean
Architecture rule). It holds exactly two new files for now
(`Time.h`/`.cpp`, `EngineContext.h`) — see PHASE1 for the full API.
`src/Core/` is deliberately not folded into `src/Math/` or anywhere else:
it is conceptually "engine bootstrap/loop state", not math, not ECS, not
any existing subsystem.

### 3.3 Locked Design Decisions (from the user's own answers — do not
relitigate these during implementation; if a phase document's plan
conflicts with one of these, the phase document is wrong and should be
fixed, not the other way around)

1. **Scope: SIMPLE, not full Unity Play/Stop.** There is no separate
   Edit-mode/Play-mode split, no scene snapshot-and-revert on "Stop". The
   game keeps running exactly as it always has; this campaign only adds
   the ability to **Pause/Resume/Step** on top of that always-running
   loop. There is no "Stop" button at all.
2. **`Game::Update()` is called every single frame, unconditionally,
   pause or not** (this is what real Unity does too —
   `Time.timeScale = 0` never stops `MonoBehaviour.Update()` from being
   called; it only makes `Time.deltaTime` read as `0`). This was an
   explicit open question the user deferred to this document to decide,
   specifically because it is the architecturally-correct choice for a
   **future** frame debugger: the whole per-frame pipeline (input
   handling, `Game::Update()`, rendering, profiling, the render graph)
   keeps executing identically every frame whether or not simulation
   actually advanced — a future frame-debugger's "step through one frame
   and inspect it" feature has nothing special to special-case. The actual
   freeze is achieved **inside** `Game::Update()` (skip the
   Animation/Physics/skinning calls entirely when frozen — cheaper than
   feeding them a zero delta and relying on each one's own degenerate-zero
   handling, though that would also be correct as a fallback).
3. **`Time` is an explicit, non-singleton object**, aggregated inside a
   new, deliberately minimal `EngineContext` struct (per the user's own
   explicit request for "an `EngineContext` to contain everything the
   engine needs, for convenience and QoL") — owned by `Application`,
   advanced once per frame, passed by `const&` into `Game::Update()`.
   `EngineContext` starts with exactly one field (`time`) — it is a real,
   intentional extension point for later engine-wide needs, not a
   speculative grab-bag to pre-populate now (YAGNI — see PHASE1's own
   header-comment wording, which mirrors `EditorContext.h`'s own existing
   "add fields only when a real need arrives" convention).
4. **Step is a fixed, deterministic 1/60s tick**, never derived from real
   elapsed wall-clock time — fully reproducible regardless of how long the
   user actually paused for or how fast they click.
5. **GPU skinning / CPU-job-dispatched skinning work is skipped entirely
   while frozen** (not merely fed a zero delta) — see the exact mechanism
   in PHASE2 (`AnimationSystem::ClearGpuSkinningDispatchThisFrame()`).
6. **No new HTTP/network endpoints in this campaign.** Pause/Resume/Step
   are Editor-toolbar-only for now; remote (AI-agent) control over HTTP is
   explicitly deferred to a possible future `network-impl-*` campaign —
   see PHASE5's `TODO.md` note.
7. **No `Time.timeScale` slider / slow-motion / fast-forward in this
   campaign.** Binary paused-or-running only. `Time`'s own API is written
   so a future `timeScale` field would be a small, additive change (not a
   redesign) — but it is not built now.
8. **No keyboard shortcut in this campaign.** Toolbar buttons only.
9. **Where state lives, reconciled:** the raw *toggle intent* (the
   `playbackPaused`/`stepOneFrameRequested` booleans) lives in
   `EditorContext` (Editor-only, per the user's own explicit answer,
   since only the Editor UI can toggle them today) — but the *time
   bookkeeping object* (`Time`, inside `EngineContext`) lives in
   engine-core, owned by `Application`, completely independent of whether
   an Editor even exists. `Application::Run()` is the one bridge that
   reads the Editor-owned intent and feeds it into the engine-core-owned
   `Time` object once per frame. This satisfies both of the user's
   answers at once (they are not actually in conflict once the toggle
   state and the bookkeeping object are recognized as two different
   things).
10. **The Editor's Scene-view camera stays fully navigable during pause**
    — confirmed to require **zero code changes** (see Step 2 above,
    `EditorCamera` is driven by raw mouse deltas, never a `Time`/delta
    value at all).
11. **Resuming from a long pause must not replay the entire elapsed
    wall-clock gap as one giant catch-up simulation step.** This is a
    correctness detail the user did not explicitly ask about but which
    this document locks in now: if the user pauses for (say) 30 real
    seconds and then resumes, the very next running frame must simulate
    one ordinary-sized step (`kFixedStepSeconds`, same constant as Step
    uses), **not** 30 seconds' worth of motion in one call (which could
    tunnel a fast-moving physics object through a thin collider, or make
    an animation clip jump forward by 30 seconds instantly). See PHASE1's
    `Time::Advance()` contract for the exact mechanism (an internal
    "was paused last call" latch).

### 3.4 Non-Goals (explicitly out of scope for this campaign)

- A "Stop" button / scene state snapshot-and-revert.
- A `Time.timeScale` slider (slow-motion/fast-forward).
- Any HTTP/network endpoint for Play/Pause/Step.
- A keyboard shortcut for Play/Pause.
- The actual frame-debugger feature itself (draw-call/render-pass
  stepping, a "Render Graph" pass inspector timeline, etc.) — this
  campaign only lays the `Time`/`EngineContext`/pause-freeze groundwork a
  later campaign can build that on top of.
- Any change to `PhysicsSystem`'s or `AnimationSystem`'s own public method
  signatures beyond the one new
  `AnimationSystem::ClearGpuSkinningDispatchThisFrame()` method (PHASE2).

### 3.5 File-change inventory (full campaign, across all phases)

New files:
- `src/Core/Time.h`, `src/Core/Time.cpp`
- `src/Core/EngineContext.h`
- `src/Editor/PlaybackControls.h`, `src/Editor/PlaybackControls.cpp`
- `tests/Core/TimeTests.cpp`
- `tests/Game/GameUpdateFreezeGatingTests.cpp`

Modified files:
- `src/Game/Game.h`, `src/Game/Game.cpp` (Update() signature + freeze gate)
- `src/Game/Animation/AnimationSystem.h` (new
  `ClearGpuSkinningDispatchThisFrame()` method)
- `src/Application/Application.h` (new `EngineContext m_engineContext;`
  member)
- `src/Application/Application.cpp` (`Run()` wiring)
- `src/Editor/EditorContext.h` (two new bool fields)
- `src/Editor/EditorLayer.h` (two new pure-virtual methods)
- `src/Editor/ImGuiEditorLayer.cpp` (implements the two new methods)
- `src/Editor/NullEditorLayer.cpp` (implements the two new methods as
  constant `false`)
- `src/Editor/DockLayout.cpp` (calls the new `BuildPlaybackToolbar()`)
- `CMakeLists.txt` (new source files added to `gte_core`)
- `tests/CMakeLists.txt` (new `Core/TimeTests.cpp` AND
  `Game/GameUpdateFreezeGatingTests.cpp` added)
- `AGENTS.md`, `README.md`, `TODO.md` (PHASE5 doc updates)

### 3.6 Order of work

Work phases 1 → 5 strictly in order; each does a fast compile check (and,
for Tier-1 test additions, a `ctest` run scoped to the new test file) before
moving on. Only PHASE5 does a full clean build + full `ctest` regression
pass + live runtime smoke test. See each phase file for its own exact
compile-check command.
