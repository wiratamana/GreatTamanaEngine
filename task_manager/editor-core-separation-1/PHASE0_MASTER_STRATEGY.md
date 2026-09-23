# PHASE0 — MASTER STRATEGY: `gte_core` / `gte_editor` Library Separation

Campaign folder: `task_manager/editor-core-separation-1/`
Source design doc (READ THIS FIRST, in full, before touching any phase):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE19_*.md`) refers back to this file as its Parent. Read this file in
full before starting ANY child phase. Every child phase must also read the
design doc above in full — it is the ground truth for WHY every phase exists,
this file is the ground truth for HOW and IN WHAT ORDER.

---

## Step 1: The Goal (Where are we going?)

Today, one CMake target, `gte_core`, contains BOTH the engine (Renderer, ECS,
Game, RenderGraph, Time/EngineContext) AND the entire Editor/debug-tooling
surface (ImGui, gizmos, Frame Debugger, panels, asset browser), gated by one
`if(GTE_ENABLE_EDITOR)` block that adds ~60 extra files into the SAME
archive. `Application` is the composition root that owns both sides.

We are changing this into **two real, separately-linked static libraries**,
built together every single time this repo is configured (no on/off switch
for whether the Editor exists):

- **`gte_core.a`** — the engine only. Zero ImGui, zero SDL, zero
  Editor/debug-tooling code, zero `#if GTE_ENABLE_EDITOR` anywhere. A new
  class, `Core` (`gte::Core`), is `gte_core`'s public facade: constructed by
  injecting an `ISurfaceProvider&` (window abstraction) and an
  `IHostServices&` (logging/host hook) — never `#include`-ing anything under
  `src/Editor/` EXCEPT `EditorLayer.h` itself (Locked Design Decision #8
  below explains this one documented exception).
- **`gte_editor.a`** — depends on `gte_core.a` (one-way, never the reverse).
  Owns ImGui, gizmos, Frame Debugger, panels, asset browser, AND the
  authoring window's own SDL/main-loop glue. A new class, `EditorHost`,
  replaces `Application` as the real composition root: it owns
  `SdlContext`+`Window`, constructs `Core` (injecting `Window` as
  `ISurfaceProvider&`), constructs the concrete Editor UI, and owns the main
  loop.
- **The executable** (`GreatTamanaEngine` today) always links `gte_editor`
  (which pulls in `gte_core` transitively) — never `gte_core` alone. It gets
  renamed at the very end of this campaign once `EditorHost` fully replaces
  `Application` (Phase 17).

**Explicitly OUT of scope**: a real, shippable Player build
(`<ProjectName>.exe` linking `gte_core.a` only, with its own window/main
loop, zero ImGui) is a SEPARATE, later initiative (Section 8 of the design
doc). This campaign proves `gte_core.a` COULD support that (via the
standalone-core probe, Phase 18) — it does not build the actual Player
tooling.

### The Four Hard Rules That Define "Done" (verbatim from the design doc, Section 1.3)

1. `gte_core.a` compiles and links standalone with **literally zero** editor
   code, zero ImGui, zero debug-only feature code, zero SDL headers in its
   own translation units — checked mechanically (Phase 18).
2. `gte_editor.a` may depend on `gte_core.a`. Never the reverse — true in
   `#include` terms, in `target_link_libraries()` terms, AND in the
   "no unresolved external symbol only `gte_editor.a` defines" sense.
3. A thin Player-build host (never having seen `gte_editor.a`'s source) can
   link `gte_core.a` alone and get a running, renderable engine.
4. `gte_editor.a` is unconditionally configured every time this repo is
   configured — no CMake option/macro anywhere skips building it.

---

## Step 2: The Situation / The Problem (Where are we now?)

Full ground truth is in the design doc's Section 2 — read it. Short version:

- `gte_core`'s `target_sources()` unconditionally lists ~230 core files, then
  an `if(GTE_ENABLE_EDITOR)` block adds ~60 more Editor files INTO THE SAME
  archive. Turning the flag off today shrinks ONE archive — it does not, and
  cannot, produce two coexisting archives with a link dependency.
- A project-wide search found `GTE_ENABLE_EDITOR` in **55 files, 118 call
  sites** — not just the two files most other analyses of this idea have
  historically focused on. All 118 sites must be gone before any CMake
  target surgery happens, or the split either fails to link or silently
  keeps the exact macro-driven branching this design removes.
- Two files (`RenderPasses.cpp`, `RenderSystem.cpp`) currently dereference a
  real, complete Editor-only type (`FrameDebuggerCaptureContext`) behind a
  macro, from code that must live in `gte_core` — a genuine link hazard, not
  just an `#include` hygiene issue.
- **A NEW finding this campaign's own investigation surfaced, absent from
  the design doc**: `Jobs/JobContinuation.cpp`, `Network/NetworkServer.cpp`,
  `Renderer/RenderGraph/RenderGraph.cpp`, and
  `Renderer/RenderGraph/RenderPassGroupRegistry.cpp` — all destined for
  `gte_core` — directly `#include "../../Editor/Logger.h"` and call the
  `GTE_LOG_*` macro, which resolves straight to the Editor-only
  `::gte::Logger` class. This is a real, live one-way-dependency violation
  the design doc's own inventory never flagged. Phase 3 fixes it.
  **Confirmed by re-reading the real source during this strategy's own
  double-check pass**: `Network/NetworkRoutes.h` ALSO directly
  `#include`s `"../Editor/Logger.h"` (line 7 of the real file today) even
  though it lives under `src/Network/` and is destined for `gte_core` —
  Phase 3 must fix this file's `#include` too, not just confirm it still
  compiles.
- **A SECOND new finding, also confirmed against the real, current
  `CMakeLists.txt`**: the root `CMakeLists.txt`'s own comment right above
  the `if(GTE_ENABLE_EDITOR)` Editor-source block states outright: *"Exactly
  one of these two branches gets compiled in, depending on
  GTE_ENABLE_EDITOR - both define the same gte::CreateEditorLayer() factory
  function... so there's never an ODR conflict."* That invariant depends
  entirely on `NullEditorLayer.cpp` and `ImGuiEditorLayer.cpp` never being
  compiled into the same final link at the same time — true today (exactly
  one `if`/`else` branch compiles), but Phase 9's own planned CMake split
  breaks it: `gte_core` keeps `NullEditorLayer.cpp` in its OWN unconditional
  source list (needed for a future Player host), while `gte_editor` — now
  ALWAYS built, per Rule 4 — carries `ImGuiEditorLayer.cpp`. Both archives
  always link into this repo's own single executable from Phase 9 onward,
  so BOTH definitions of `gte::CreateEditorLayer()` would exist in the same
  final link, forever, with no compiler/linker error guaranteed to catch it
  (a static archive silently never extracts a member whose symbol is
  already resolved — this can silently pick either implementation depending
  on archive scan order). Locked Design Decision #9 below fixes this at the
  root.
- `Application` (today's composition root) owns SDL, Window, Renderer,
  RenderGraph, Game, AND the Editor, all as direct members — a third party
  owning both sides is exactly what this campaign inverts. Its per-frame
  loop (`Application::Run()`) calls into its owned `IEditorLayer`
  (`m_editorLayer`) at roughly 30 separate call sites, threaded pervasively
  through the exact orchestration body Phase 13 moves into `Core` — Locked
  Design Decision #8 below draws the line for exactly which of those call
  sites move into `Core` and which stay a host-level (`Application`/
  `EditorHost`) concern.
- `tests/CMakeLists.txt`'s own header comment already documents the concrete
  cost of this: the test binary loads `SDL3.dll` at process start purely
  because `Window.cpp` sits in the same archive it links, even though no
  Tier-1 test ever opens a window.

---

## Step 3: The Plan — Locked Design Decisions (confirmed via `ask_questions`, DO NOT re-litigate these)

1. **Logging fix (new gap, not in the original design doc)**: `GTE_LOG_*`
   and the plain `LogLevel`/`LogEntry`/`LogQueryFilter` types move into a
   NEW `gte_core`-owned header (`src/Core/Logging.h` — living next to the
   already-existing `src/Core/Time.h`/`EngineContext.h`). `GTE_LOG_*`
   compiles UNCONDITIONALLY (no `#if GTE_ENABLE_EDITOR` branch at all,
   ever) and routes through a global, registrable `ILogSink` interface
   (`src/Core/LogSink.h`) — install-once, idempotent, mirroring
   `SdlMemoryTracker`'s existing all-static/global precedent. Editor's
   `Logger` class becomes ONE concrete `ILogSink` implementation,
   registered once by `EditorHost` at startup. A Player host that never
   registers a sink gets a safe, silent no-op automatically — no macro
   needed for that "OFF" behavior at all.
2. **Bucket B capability interfaces**: MANY small, single-purpose
   interfaces — one per capability — never one big "god interface".
   Mirrors the already-proven `FrameDebuggerCaptureContext*` opaque-pointer
   shape exactly: `Core` (or the relevant Layer-2-adjacent header) holds a
   nullable pointer/reference to each small interface, supplied by whoever
   composes the app (`EditorHost` supplies real ones; a Player host supplies
   `nullptr` for all of them). This rule governs ONLY the genuinely NEW
   Bucket B capability gaps (scene IO, Editor UI commands, asset import,
   GPU-driven-batch test spawning) — it does NOT apply to the two
   ALREADY-DESIGNED, pre-existing hooks (`IEditorLayer*`,
   `FrameDebuggerCaptureContext*`), which stay exactly as large/small as
   they already are (see Locked Design Decision #8).
3. **Executable rename IS in scope** — Phase 17 renames the built
   executable target away from `GreatTamanaEngine` once `EditorHost` fully
   replaces `Application`, as the last CODE step of this campaign.
4. **Full `ctest` regression cadence**: every phase does an INCREMENTAL
   compile-check only (never a full rebuild, never a full `ctest` run),
   EXCEPT three specific checkpoints, which each run a FULL clean build +
   full `ctest` regression pass:
   - **Phase 9** (CMake target split) — the design doc's own
     highest-risk step.
   - **Phase 14** (Window/SDL fully out of `gte_core`) — the design doc's
     other flagged highest-risk step.
   - **Phase 19** (campaign close-out) — the final, mandatory full
     verification.
   Every other phase report must explicitly state "incremental compile
   check only, per campaign policy" — do not silently skip this note.
5. **CI-only standalone-core probe**: this repo has NO real CI pipeline of
   its own (verified — only vendored `third_party/**/.github` workflows
   exist, nothing for this project). Phase 18's probe is therefore a
   manually-invocable local CMake project under
   `tools/ci/gte_core_standalone_probe/`, documented with the exact command
   to run it by hand — NOT a GitHub Actions workflow. Do not invent a real
   CI pipeline as part of this campaign.
6. **`GTE_ENABLE_PROJECT_PANEL`** (the nested switch inside the old
   `GTE_ENABLE_EDITOR` block, gating `ProjectPanelData`/`AssetInspectorData`/
   etc.) is ORTHOGONAL to this campaign and stays untouched — it becomes a
   switch purely INSIDE `gte_editor`'s own source list (still gating which
   Editor-only files compile, never touching `gte_core`). Do not remove it,
   do not "fix" it, it is out of scope.
7. **`gte_core` the CMake target name, and `Core` the C++ class name, are
   both already correct for the end state and are NEVER renamed.**
8. **`Core` holds exactly ONE nullable `IEditorLayer*` hook — the "big"
   sibling of `FrameDebuggerCaptureContext*`'s own already-proven
   small-scale opaque-pointer pattern, NOT a new Bucket-B-style micro
   interface, and NOT split into smaller pieces.** `IEditorLayer`
   (`src/Editor/EditorLayer.h`) already correctly abstracts the whole Editor
   away from `Application` today (design doc Section 2.4: "the exact pattern
   needed one boundary earlier... it just needs its ownership direction
   reversed"). This campaign's job is to relocate that pointer's OWNERSHIP
   (Editor owns Core, never the reverse) and split which of its ~30 call
   sites actually belong inside `Core::BuildFrame()` versus which stay a
   host-level concern — never to redesign or fragment the interface itself.
   `EditorLayer.h` stays the ONE documented exception to "`gte_core` never
   includes anything under `src/Editor/`" (the design doc's own Section 10
   checklist says exactly this: "...contain zero `#include` of anything
   under `src/Editor/` **except `EditorLayer.h` itself**"). Concretely:
   - Only these `IEditorLayer` methods are ever called from inside `Core`
     (through a null-checked `m_editorLayer` pointer, Player host passes
     `nullptr`), because they genuinely participate in RENDER-GRAPH FRAME
     BUILDING: `GameViewTarget()`, `SceneViewTarget()`,
     `SceneViewProjection()`, `SceneViewCameraWorldPosition()`,
     `RenderSceneGrid()`, `AddBlurValidationPass()`/
     `FinalizeBlurValidationForSampling()`, `AddGBufferValidationPass()`/
     `FinalizeGBufferValidationForSampling()`,
     `SetGameViewCompositedTexture()`/`SetSceneViewCompositedTexture()`,
     `PrepareFrameDebuggerCaptureContext()`,
     `ConsumePendingFrameDebuggerReplayRequest()`.
   - Every OTHER `IEditorLayer` method — `NewFrame()`, `BuildUI()`,
     `Render()`, `RenderPlatformWindows()`, `ProcessEvent()`,
     `OnWindowResized()`, `WantsCaptureMouse()`/`WantsCaptureKeyboard()`,
     `WantsExit()`, `IsPlaybackPaused()`/`TryConsumeStepRequest()`/
     `NotifyFrameDebuggerStepConsumed()`, `ActivateTab()`,
     `ImportExternalAssetIntoProject()`, `SpawnGpuDrivenTestBatch()`, and
     every Frame-Debugger-UI-state method (`FrameDebuggerOpenWindow()`,
     `FrameDebuggerSetEnabled()`, etc.) — is a host-level (input routing /
     UI building / automation) concern. These stay on
     `Application`/`EditorHost`, called directly against the same
     `IEditorLayer` instance, interleaved AROUND (never inside) `Core`'s own
     `Update()`/`BuildFrame()`/`Present()` calls. `Core` never calls these.
   Phase 12 declares the hook; Phase 13 performs the actual call-site split;
   Phase 15/16 wire it up on the `EditorHost` side.
9. **`src/Editor/NullEditorLayer.cpp`'s factory function is renamed from
   `CreateEditorLayer()` to `CreateNullEditorLayer()` as part of Phase 9**,
   to remove the confirmed ODR/link-order hazard described in Step 2 above.
   After this rename: `gte::CreateEditorLayer()` has exactly ONE definition
   in the entire link (`ImGuiEditorLayer.cpp`, `gte_editor`-only) forever
   after Phase 9 — `EditorHost` (Phase 15) keeps calling it unambiguously.
   `gte::CreateNullEditorLayer()` is a second, distinctly-named,
   always-available fallback declared alongside it in `EditorLayer.h`
   (the same documented `gte_core`-visible exception file), defined only in
   `gte_core`'s own `NullEditorLayer.cpp`, and is never called by anything
   in THIS repo — it exists purely so a future Player host (linking
   `gte_core` alone, never seeing `gte_editor`'s source) has something to
   call for a no-op Editor implementation, satisfying Rule 3.

---

## Step 3 (continued) — Phase Sequence and Dependency Order

Execute STRICTLY in this order — each phase assumes every earlier phase is
already done. Do not skip ahead.

| # | File | One-line summary | Full/extra ctest? |
|---|------|-------------------|--------------------|
| 1 | `PHASE1_SDL_DEPENDENCY_REGRESSION_BASELINE.md` | Capture today's "test binary needs SDL3.dll" as a provable, standing regression check | No |
| 2 | `PHASE2_FRAME_DEBUGGER_CAPTURE_POINTER_SAFETY_FIX.md` | Relocate the two real complete-type dereferences (`RenderPasses.cpp`, `RenderSystem.cpp`) out of core-destined code | No |
| 3 | `PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md` | Fix the newly-found Logger/`GTE_LOG_*` one-way-dependency violation via `ILogSink` | No |
| 4 | `PHASE4_GPU_MEMORY_TRACKER_BUCKET_A_EXTRACTION.md` | Extract editor-only debug-name tracking out of `GpuMemoryTracker` et al. | No |
| 5 | `PHASE5_EDITOR_CAPABILITY_INTERFACES_DESIGN.md` | Declare the small, per-capability runtime interfaces (Bucket B, part 1: design + declare) | No |
| 6 | `PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md` | Convert `EngineCommandDispatch.cpp` + `NetworkServer.cpp` scene save/load macro checks to runtime checks | No |
| 7 | `PHASE7_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_UI_ASSET_TESTSPAWNER.md` | Convert the remaining Bucket B sites (`EditorUiCommandBridge.h`, `AssetImportCommandBridge.h`, `GpuDrivenBatchTestSpawner.h`, `Game.h`) | No |
| 8 | `PHASE8_BUCKET_C_DEAD_BRANCH_CLEANUP_AND_MACRO_DELETION.md` | Delete dead Editor-only `#else` branches, delete the `GTE_ENABLE_EDITOR` CMake option + compile definition, confirm zero remaining references anywhere | No |
| 9 | `PHASE9_CMAKE_TARGET_SPLIT.md` | Create real `gte_editor` target, move the fixed file list, rename `NullEditorLayer`'s factory to remove the ODR hazard, always link `gte_editor` | **FULL ctest (checkpoint 1)** |
| 10 | `PHASE10_ISURFACEPROVIDER_INTERFACE_AND_WINDOW_INVERSION.md` | `ISurfaceProvider` interface; `Window` implements it; static→virtual method fix | No |
| 11 | `PHASE11_PROFILING_SDL_CLOCK_LEAK_FIX.md` | Hide `SDL_GetPerformanceCounter`/`Frequency` behind an internal clock function | No |
| 12 | `PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md` | New `Core` class: members + constructor + accessors + the nullable `IEditorLayer*` hook (no orchestration logic yet) | No |
| 13 | `PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md` | Move `Application::Run()`'s per-frame orchestration body into `Core::Update()`/`BuildFrame()`/`Present()`, splitting `IEditorLayer` call sites per Locked Design Decision #8 | No |
| 14 | `PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md` | `Window.cpp`/`SdlContext` move into `gte_editor`; `gte_core` drops `SDL3::SDL3` at link time; re-verify Phase 1's regression flips | **FULL ctest (checkpoint 2)** |
| 15 | `PHASE15_EDITORHOST_COMPOSITION_ROOT_CORE_CONSTRUCTION.md` | New `EditorHost` class: owns `SdlContext`+`Window`, constructs `Core`, constructs Editor UI, wires the `IEditorLayer*` hook | No |
| 16 | `PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md` | `EditorHost::Run()` main loop; attach automation bridges (never to `Core`) | No |
| 17 | `PHASE17_APPLICATION_RETIREMENT_AND_EXECUTABLE_RENAME.md` | Retire `Application.h/.cpp`; rename the executable target | No |
| 18 | `PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md` | Fake/headless `ISurfaceProvider` test fixture; manually-invocable `gte_core`-only standalone probe | No (probe build only) |
| 19 | `PHASE19_FINAL_FULL_REGRESSION_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` | Mechanically re-check every item in the design doc's Section 10 checklist; full clean build + full `ctest`; live HTTP-driven smoke test; `CAMPAIGN_COMPLETION_REPORT.md` | **FULL ctest + full clean build (final)** |

---

## Universal Rules Every Phase Must Follow

1. **Read this file (Parent) and the design doc in full before starting.**
   Read every `PHASEn_COMPLETION_REPORT.md` left by a prior phase in this
   same folder — it may contain a clue, a deviation, or a discovered fact
   the next phase needs (e.g. an exact file path this strategy could not
   verify in advance).
2. **Stay on branch `feature/editor-core-separation`.** Never switch
   branches.
3. **Namespace/RAII/Clean-Architecture conventions from `AGENTS.md` apply to
   every new file.** Every new type lives in `namespace gte { ... }`.
4. **No full build/full `ctest` except the three checkpoints listed above.**
   Every other phase does an incremental compile check only (build just the
   changed target(s), e.g. `cmake --build build --target gte_core` or
   similar) plus, where relevant, a quick `run_app_background` +
   `gte_send_request` visual/log smoke check — never a full clean rebuild.
5. **Use the engine's own logging + `gte_send_request` for debugging.**
   Never add `std::cout`/`printf`/`OutputDebugString` debug prints — use
   `GTE_LOG_*` and pull logs back via `GET /get_logs`.
6. **Every phase must end with**: a compile-check result, a
   `PHASEn_COMPLETION_REPORT.md` written into this same folder, and a git
   commit (`git_add` + `git_commit`) of both the code changes and that
   report.
7. **Every phase-implementation task must call `ask_questions`** if it hits
   a genuine ambiguity this strategy doc does not resolve — do not guess
   silently on anything architecturally significant.
8. **Implementation phases must NOT call `delegate_task`.** Only the
   orchestrator/double-check pass is allowed to delegate. A phase that
   discovers it is too large mid-flight should say so honestly in its
   completion report rather than spawning sub-tasks itself.
9. **If a phase's own exact file paths differ from what this strategy
   assumed** (the design doc itself admits its own file inventory is
   "representative, not necessarily exhaustive" for the 55-file macro
   sweep), use `search_in_dir` to re-confirm the real, current location
   before editing — never edit blind against a guessed path.

---

## Non-Goals (repeat, do not implement these under this campaign)

- The Player Build Pipeline (Section 8 of the design doc) — no
  `<ProjectName>.exe` generation, no per-project build system, no Player
  host template beyond what Phase 18's standalone-core probe needs.
- A real `.dll`/shared-library boundary, hot-swap, or independent runtime
  versioning between `gte_core`/`gte_editor` (Section 3 of the design doc)
  — both stay static archives, baked into one executable at final link.
- A real CI pipeline for this repository (Locked Decision #5 above).
- Any change to `GTE_ENABLE_PROJECT_PANEL` (Locked Decision #6 above).
- Any change to `GTE_ENABLE_PROFILER`/`GTE_ENABLE_NETWORK` — separate,
  independent switches, unaffected by this design.
- Redesigning or fragmenting `IEditorLayer` itself (Locked Decision #8) —
  only its ownership and which-subset-runs-where change, never its own
  method set/shape.
