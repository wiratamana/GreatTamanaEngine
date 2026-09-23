# editor-core-separation-1 — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level summary of the whole
19-phase campaign, mirroring `task_manager/render-pass-7/CAMPAIGN_COMPLETION_REPORT.md`'s own shape.
See each `PHASEn_COMPLETION_REPORT.md` in this same folder for full per-phase detail.

## What this campaign set out to do

Turn one CMake target (`gte_core`, ~290 files gated by one `if(GTE_ENABLE_EDITOR)` block containing
BOTH the engine and the entire Editor/debug-tooling surface) into **two real, separately-linked
static libraries** — `gte_core.a` (engine only, zero ImGui/SDL/debug-tooling) and `gte_editor.a`
(depends on `gte_core.a`, owns ImGui/gizmos/Frame Debugger/panels/the authoring window/main loop) —
with a new `Core` class as `gte_core`'s public facade and a new `EditorHost` class replacing
`Application` as the real composition root, per
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`.

## What shipped, phase by phase

**PHASE1 — SDL Dependency Regression Baseline.** A pure PE/COFF import-table parser
(`tests/Build/SdlLinkageRegressionTests.cpp`) proved, mechanically, that `GreatTamanaEngineTests.exe`
required `SDL3.dll` at the time — the concrete "before" state Phase 14 later flips (partially).
Deviation: abandoned a `CreateProcessW()`-based "launch without SDL3.dll" probe after it triggered a
real, blocking Windows dialog and an antivirus-related hang on this machine; the user steered toward
the PE-parser approach live.

**PHASE2 — Frame Debugger Capture Pointer Safety Fix.** Moved `AddFrameDebuggerReplayPasses()`'s and
`RenderSystem::Draw()`'s real complete-type-dereferencing bodies out of `gte_core`-destined files
into two new `gte_editor`-destined files (`FrameDebuggerReplayPasses.cpp`, declared-but-defined-
elsewhere `RecordFrameDebuggerDraws()`). **Confirmed, accepted, temporary regression** (via
`ask_questions`): `GTE_ENABLE_EDITOR=OFF` stopped LINKING from this phase forward (two `undefined
reference` errors) — deliberately left broken per explicit user sign-off, since PHASE8/Bucket B was
expected to eventually resolve it (see "Honest, unresolved gap" below — it did not, in the way this
campaign needed).

**PHASE3 — Logging Global `ILogSink` Extraction.** New `src/Core/Logging.h`/`LogSink.h/.cpp`:
`GTE_LOG_*` now compiles unconditionally and routes through a registrable `ILogSink`. `Editor::Logger`
becomes one concrete sink. **Discovered gap, documented not silently patched**: `NetworkServer.cpp`/
`NetworkRoutes.cpp`/`.h` have a real, pre-existing, ALREADY-DOCUMENTED (`AGENTS.md`, "Logging") direct
dependency on the real `Logger` class (`/get_logs`/`/clear_logs`), outside this phase's own declared
scope to fix — flagged loudly for whoever eventually closes it (nobody in this roadmap did).

**PHASE4 — GPU Memory Tracker Bucket A Extraction.** `GpuMemoryTracker` gained an always-compiled
`DebugNameObserver` hook; all name STORAGE moved to a new `EditorGpuMemoryNameOverlay` (Editor-owned).
Confirmed, by reading every relevant constructor, that `Install()` must run before
`ImGuiEditorLayer`'s own member-initializer list (not inside its constructor body) or the first two
named GPU resources silently lose their names.

**PHASE5 — Bucket B Design.** Of the four candidate capability interfaces the strategy sketched, only
**one** was a genuinely new gap: `ISceneIOCapability`. The other three (Editor UI activation, asset
import, GPU-driven-batch test spawning) were already fully answered by the pre-existing
`IEditorLayer*` opaque-pointer hook (`NullEditorLayer` already gives a correct "unavailable" answer)
— confirmed by reading `Application.cpp`'s real call sites, not assumed.

**PHASE6 — Scene IO Call-Site Conversion.** `EngineCommandDispatch.cpp`'s `#if GTE_ENABLE_EDITOR`
scene save/load became a runtime null-check against `ISceneIOCapability*`, backed by a new
`EditorSceneIOCapability` adapter. `ISceneIOCapability` gained a necessary third method,
`DefaultScenePath()`, discovered mid-phase (the interface as PHASE5 declared it had no way to resolve
an empty caller-supplied path).

**PHASE7 — Remaining Capability Call-Site Conversion.** **Zero code changes required** — re-confirmed,
independently, PHASE5's own finding that `EditorUiCommandBridge.h`/`AssetImportCommandBridge.h`/
`GpuDrivenBatchTestSpawner.h`/`Game.h` never needed a new interface at all.

**PHASE8 — Bucket C Dead-Branch Cleanup + Macro Deletion.** Deleted the `GTE_ENABLE_EDITOR` CMake
`option()` and its compile definition outright; every real `#if`/`#else`/`#endif` region removed (5
in `src/`, 2 in `tests/`, beyond the phase's own original file list — found and closed via a full
repo-wide sweep). **Genuine ambiguity resolved via `ask_questions`** (user: "leave the decision making
up to you"): `NetworkServer.cpp`'s `/spawn_gpu_driven_test_batch` route used to decide 503-vs-400 by
substring-searching `errorMessage` for the literal text `"GTE_ENABLE_EDITOR"` — a hazard the phase's
own prose-cleanup was about to silently break. Fixed by adding a real `bool editorAvailable` field
(mirroring `SaveSceneOutcome`'s own precedent) instead of string-sniffing.

**PHASE9 — The CMake Target Split (Checkpoint 1).** Real `add_library(gte_editor STATIC ...)` created;
`NullEditorLayer`'s factory renamed `CreateNullEditorLayer()` (Locked Design Decision #9, closing the
ODR/link-order hazard). **A real, temporary static-archive link-order problem was found and fixed**:
`Application.cpp` (still `gte_core`-resident) called several functions only `gte_editor` now defines —
fixed with the standard `$<LINK_GROUP:RESCAN,gte_editor,gte_core>` generator expression, expected to
become unnecessary once PHASE13 removed `Application`'s direct Editor-function calls (see "Honest,
unresolved gap" below — this expectation only partially came true). **Full clean rebuild: 493/493
steps. Full `ctest`: 1771 passing / 1772 total (1 pre-existing skip) — the FIRST full run this whole
campaign ever performed** (every phase before this used incremental/targeted runs only, per policy).

**PHASE10 — `ISurfaceProvider` + Window Inversion.** New `src/Core/ISurfaceProvider.h`; `Window`
implements it; `Renderer`/`VulkanSurface` depend on the interface, never the concrete `Window`, class.
`Window::VulkanInstanceExtensions()` converted `static` → virtual.

**PHASE11 — Profiling SDL Clock Leak Fix.** `ScopeTimer.h`/`JobScopeTimer.h`'s inline
`SDL_GetPerformanceCounter()`/`Frequency()` calls hidden behind a new `Profiling::ProfilingClock.h`
indirection (still SDL-backed at this point — PHASE14 later swaps the implementation to
`std::chrono`).

**PHASE12 — `Core` Class Skeleton.** New `gte::Core` (constructor, accessors, the nullable
`IEditorLayer*` hook). **Two deviations, resolved via `ask_questions`** (user deferred): `Application`
keeps reference-alias members (`Renderer&`, etc.) bound to `Core`'s real instances instead of a
literal `m_core.GetX()` rewrite everywhere (deferred to PHASE13, where it becomes moot); the two
`rg::RenderPipeline` instances stay `Application`-owned this phase (moved in PHASE13 with their real
consumer).

**PHASE13 — Core Frame Orchestration Extraction.** The design doc's own explicitly-named highest-risk
step. `Application::Run()`'s entire per-frame body (2506 lines) classified into exactly 13
render-graph-frame-building `IEditorLayer` call sites (moved into `Core::BuildFrame()`, behind the
null-checked hook) and 22 host-level call sites (stayed on `Application`) — Locked Design Decision #8's
own two lists turned out to be exhaustive. One genuinely new wiring shape needed:
`Core::SetPresentImGuiRecorder()` (a `std::function` `Core` stores/forwards but never calls
`IEditorLayer::Render()` itself). `GpuDrivenBatchDebugInfo` relocated out of `EditorLayer.h` into a new
`src/Renderer/Culling/GpuDrivenBatchDebugInfo.h` so `Core.h` could expose an accessor for it without
including anything under `src/Editor/`.

**PHASE14 — Window/SDL Fully Out of `gte_core` (Checkpoint 2).** **The phase's own plan text was
factually wrong about scope** — live re-investigation found `SdlContext` is a private nested struct
inside `Application` (not standalone), and `Application.cpp`/`EventTranslator.h/.cpp`/
`Memory/SdlMemoryTracker.h/.cpp` all directly used SDL and were still `gte_core`-resident; a SECOND
real leak, `Profiling/ProfilingClock.cpp` (PHASE11's own fix) still called raw SDL underneath, caught
live by a new archive-content regression test failing on the first attempt. Fixed by moving all of
these into `gte_editor`'s source list and rewriting `ProfilingClock` to use `std::chrono::steady_clock`.
**A second discovery**: Phase 1's original regression test can never flip to "SDL3.dll absent" as the
strategy assumed — `Memory/SdlMemoryTrackerTests.cpp` is a real, permanent, direct-SDL test compiled
straight into the test executable regardless of which static library anything lives in. A NEW,
actually-achievable test, `GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols`, replaces it as the
real "flip": scans `gte_core.a`'s own built bytes for 11 real SDL3 symbol names, finds none. **Full
clean rebuild: 495/495 steps. Full `ctest`: 1772 total, 1771 passed, 1 pre-existing skip** — same
passing count as Phase 9 despite one new test being added (one other, unrelated test's count changed
somewhere across the untested Phases 10-13; honestly flagged, not chased down, zero new failures
either way).

**PHASE15 — `EditorHost` Composition Root Construction.** New `EditorHost` (owns `SdlContext`+
`Window`, constructs `Core`, calls the ONE new wiring line this whole campaign exists to add —
`Core::SetEditorLayerHook()`). `Run()` was a deliberate, documented TEMPORARY stub (no
`Core::Update()`/`BuildFrame()`/`Present()`, no `NetworkServer`) — `main.cpp` already pointed at
`EditorHost`, but `Application` (unused, untouched) was still the only thing that actually rendered.

**PHASE16 — `EditorHost::Run()` Main Loop + Automation Bridges.** Every automation bridge
(`EngineCommandBridge`/`FrameCaptureBridge`/`EditorUiCommandBridge`/`FrameDebuggerCommandBridge`/
`AssetImportCommandBridge`) plus the embedded `Network::NetworkServer` moved onto `EditorHost`, never
`Core` (confirmed: `Core.h`/`Core.cpp` needed zero changes this phase). `Run()`'s real body copied
line-for-line from `Application::Run()`'s own post-PHASE13 shape. `Application.cpp` shrank from 918 to
216 lines — genuinely dead code from this point on, kept only because it still compiled.

**PHASE17 — Retire `Application`, Rename the Executable.** `Application.h`/`.cpp` deleted outright.
Executable renamed `GreatTamanaEngine` → **`GreatTamanaEditor`** (chosen via `ask_questions`, user
deferred — the design doc's own Section 6.2 suggestion, echoed by the strategy file, the lowest-risk
campaign-endorsed choice). **A stale prediction corrected, not silently left wrong**: Phase 9's own
comment predicted the `$<LINK_GROUP:RESCAN,...>` wrinkle would disappear once PHASE13 landed — this
turned out only partially true: `RenderSystem.cpp`'s call into `RecordFrameDebuggerDraws()` and
`NetworkServer.cpp`'s calls into `Logger::Query()`/`Clear()` are real, PERMANENT `gte_core →
gte_editor`-only-symbol dependencies with no further phase in the roadmap planned to close them —
this is the exact gap PHASE19's own investigation (below) empirically proves is real.

**PHASE18 — Headless Test Fixture + Standalone-Core Probe.** `tests/Fakes/HeadlessSurfaceProvider.h`
(real `VK_EXT_headless_surface`-based `ISurfaceProvider`, never a fake pointer) +
`tests/Core/CoreHeadlessConstructionTests.cpp`. **Real abandoned first attempt, documented**: the
plan's own suggested `add_subdirectory()`-based probe genuinely failed (every `cmake/Fetch*.cmake`
hardcodes `CMAKE_SOURCE_DIR`, not `CMAKE_CURRENT_SOURCE_DIR`) — replaced with a tiny meta-project that
shells out to a completely separate, nested `cmake -S <real root> -B <private dir>
-DGTE_CORE_STANDALONE_PROBE_ONLY=ON` invocation. Run for real this phase: **224/224 steps, zero
gte_editor/SDL/ImGui in the produced build tree.** The headless test itself **legitimately skips** on
this development machine (`vkCreateInstance` → `VK_ERROR_EXTENSION_NOT_PRESENT` — this machine's
Vulkan driver/loader does not report `VK_EXT_headless_surface`), exactly matching `AGENTS.md`'s own
pre-existing, pre-campaign disclosure that headless mode isn't available here.

**PHASE19 (this phase) — Final Full Regression Verification and Campaign Closeout.** See the dedicated
sections below for the full mechanical re-check, the full clean build/`ctest` results, the live smoke
test, and — most importantly — a new, load-bearing finding this phase's own investigation surfaced
that no prior phase's own narrower verification could have caught.

---

## PHASE19's own mechanical re-check of the design doc's Section 10 checklist

1. **`gte_core`'s `target_link_libraries()` never lists `gte_editor`/`SDL3`/`imgui`/`imguizmo`** — YES,
   confirmed by directly reading the final `CMakeLists.txt`: `gte_core` links only `volk`/`vma`/
   `stb_image`/`stb_image_write`/`KTX::ktx`/`httplib`/`nlohmann_json`/`Threads::Threads`/`saba_pmx`.
   `gte_editor` links `gte_core` (`PUBLIC`), `imgui`/`imguizmo` (`PRIVATE`), `SDL3::SDL3` (`PUBLIC`) —
   the one-way direction is correct.

2. **`gte_core`'s own `.cpp`/`.h` files contain zero `#include` of anything under `src/Editor/` except
   `EditorLayer.h`, and zero `#if`/macro conditioned on "is this an Editor build"** —
   **PARTIALLY TRUE, NOT FULLY TRUE.** A fresh, repo-wide `search_in_dir` for `GTE_ENABLE_EDITOR`
   confirms **zero real preprocessor directives or CMake conditionals remain anywhere** (all ~114
   remaining matches across `src/`+`tests/` are prose comments/doc-strings/deliberately-preserved
   runtime fallback-message text, exactly as Phases 3/6/8 each already flagged and accepted). But a
   regex search for `#include\s*"[./]*Editor/` scoped outside `src/Editor/` finds **THREE real,
   unconditional, non-`EditorLayer.h` includes, all inside `gte_core`'s own source list**:
   `src/Network/NetworkRoutes.h` → `Editor/EditorPanelCatalog.h`, `src/Network/NetworkRoutes.cpp` →
   `Editor/Logger.h`, `src/Network/NetworkServer.cpp` → `Editor/Logger.h`. All three are the SAME,
   already-disclosed gap `PHASE3_COMPLETION_REPORT.md` flagged ("Discovered gap... not silently
   patched over") and `PHASE7`/`PHASE17` each re-confirmed still open — never fixed by any of the 19
   phases, since fixing it needs a new Bucket-B-style opaque log-query capability that no phase in
   this roadmap was ever assigned to design.

3. **`gte_core` builds and links standalone via the standalone-core probe** — YES, re-run fresh this
   phase (`cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe -G Ninja && cmake --build
   build-core-probe`): 224/224 steps, zero errors, the produced tree contains only `libgte_core.a`/
   `libsaba_pmx.a`/`libvolk.a`/`_ktx_build/` — no `gte_editor`, no ImGui, no SDL3, no executable.

4. **`GreatTamanaEngineTests` no longer requires `SDL3.dll` for the wrong reason** — YES, re-confirmed:
   `SdlLinkageRegressionTest.SdlLinkageRegression_TestBinaryCurrentlyRequiresSdl3Dll` still passes
   (SDL3.dll IS present — for the correct, permanent, documented reason:
   `Memory/SdlMemoryTrackerTests.cpp`'s own real, direct SDL calls), and
   `GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols` still passes (`gte_core.a` itself carries
   zero real SDL3 function symbols).

5. **The headless `ISurfaceProvider` test actually runs as part of the real test suite** — YES: it is
   test #1742 of 1773 in the full `ctest` run below, discovered and executed by `ctest` like every
   other test, reporting a real, legitimate, environment-gated `GTEST_SKIP()` (this development
   machine's Vulkan driver does not support `VK_EXT_headless_surface`) — not silently omitted from the
   suite.

6. **No object file inside `gte_core.a` carries an unresolved external symbol only `gte_editor.a`
   defines — the probe IS this check; confirm it is not a trivially-always-passing no-op** —
   **THE PROBE ITSELF PASSES A DELIBERATE-VIOLATION TEST (a stray `#include <imgui.h>` inside
   `src/Core/Core.cpp` was added, confirmed to make the probe genuinely FAIL to compile with `fatal
   error: imgui.h: No such file or directory`, then reverted — confirmed byte-identical to HEAD via
   `git hash-object` matching `git ls-tree`'s blob hash exactly) — so the probe is real, not a no-op,
   for a MISSING-DEPENDENCY-style violation.**
   **However, a deeper, genuinely new finding this phase's own investigation surfaced, going beyond
   what the deliberate-violation test alone would have shown**: the probe **only builds `gte_core` as
   a static archive — it never links an executable** — and archiving object files (`ar`/CMake's static
   library step) never resolves symbols across translation units. This means the probe is
   **structurally incapable of catching an UNRESOLVED-SYMBOL-style violation** (as opposed to a
   missing-header-style one), which is exactly the shape of the two pre-existing, already-documented
   gaps above. To confirm this empirically rather than reason about it, this phase compiled and linked
   a small, throwaway, NEVER-COMMITTED probe program (`gte_core_link_probe.cpp`, deleted immediately
   after) that calls `RenderSystem::Draw()` — forcing `RenderSystem.cpp.obj` to be pulled from
   `libgte_core.a` — against `libgte_core.a` and its real dependencies **alone, with no `gte_editor.a`
   at all**. The link **failed**, for real, with:
   ```
   undefined reference to `gte::RecordFrameDebuggerDraws(gte::FrameDebuggerCaptureContext&,
   gte::Registry&, gte::Renderer&, gte::Entity, gte::Mesh const&, gte::Pipeline const&,
   gte::MaterialTexture const*, gte::Mat4 const&)'
   ```
   This is the mechanical, empirical proof — not just "confirmed by direct code reading" (`PHASE17`'s
   own phrasing) — that **the design doc's own Section 10 claim for this bullet does not hold today**,
   and that the standalone-core probe as built cannot be extended to catch it without also building a
   real Player-host-style executable link target, which is itself explicitly out of this whole
   campaign's own scope (the Player Build Pipeline, Section 8). This is a genuine, structural tension
   inside the design doc itself, not a mistake by any one phase.

## Full clean build + full `ctest` regression pass (the final, mandatory checkpoint)

- **Full clean rebuild** (`build/` deleted, reconfigured, rebuilt from nothing): **497/497 steps, zero
  errors.** (Up from Phase 9's 493/Phase 14's 495 — the delta is exactly the new files/probe machinery
  Phases 10-18 added.)
- **Full `ctest -C Debug --output-on-failure`**: **1773 total tests, 100% of executed tests passing
  (1771/1771), 2 legitimate, documented, environment-gated skips** — zero failures:
  - `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` (pre-existing, gated on a real
    MMD model file not present on this machine, documented long before this campaign).
  - `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
    (new, PHASE18 — this machine's Vulkan driver lacks `VK_EXT_headless_surface`).
- **Before/after comparison against this campaign's own pre-existing baselines**: Phase 9's checkpoint
  recorded 1771 passing / 1772 total (1 skip); Phase 14's checkpoint recorded the identical 1771
  passing / 1772 total (1 skip). This final run: **1771 passing / 1773 total (2 skips)** — the
  passing count is unchanged, the total grew by exactly 1 (Phase 18's own new headless test), and that
  one new test is itself the second, newly-legitimate skip. **Zero new failures anywhere across the
  entire campaign's full history of full-suite checkpoints.**

## Live, HTTP-driven end-to-end smoke test

Booted the renamed `build/GreatTamanaEditor.exe` via `run_app_background`, drove it via
`gte_send_request`:

| Endpoint | Result |
|---|---|
| `GET /get_swapchain` | `200` — real, correctly-rendered Editor UI (Hierarchy/Scene/Game/Inspector/Memory/Profiler/Render Graph/Atmosphere/Jobs/Log/Project, Pause/Step toolbar) |
| `GET /get_game_view` | `409` initially (Game tab not yet active — correct, existing behavior), `200` with a real sky-gradient PNG after `GET /activate_tab?name=Game` |
| `GET /list_tabs` | `200` — 11 known panel names |
| `GET /get_logs?limit=20` | `200` — real startup entries, including the `"EditorHost"` construction-confirmation log line |
| `POST /save_scene` (`{"path":""}`) | `200`, `success:true`, correct resolved default path |
| `POST /load_scene` (`{"path":""}`) | `200`, `success:true`, same file round-tripped |
| `POST /spawn_gpu_driven_test_batch` (`{"instanceCount":6}`) | `200`, `{"instance_count":6,"success":true}` |
| `GET /frame_debugger/open` | `200`, `windowOpen:true` |
| `GET /frame_debugger/enable?value=true` | `200`, `enabled:true` |
| `GET /frame_debugger/capture` | `200`, `hasCapturedFrame:true`, `totalEventCount:17` |
| `GET /get_swapchain` (final) | `200` — Frame Debugger tree shows real "Compute LUT"/"RenderOpaque"/"Compute Dispatches (Post-GameView)" groups exactly matching every prior campaign's own documented shape |
| `GET /get_logs?min_level=Warning&limit=50` | `200`, 3 entries — all the SAME pre-existing, documented `RenderGraphNameSlotTable` GPU-timing-slot-budget-exhaustion warning from the unrelated `render-pass-6` campaign (fires when >16 named passes compete for the fixed timing-slot budget, exactly as `AGENTS.md`'s own "Render Pass System" section already documents) — zero new warning/error category |

`stop_app_background`'d the process cleanly when done. Every endpoint behaves identically to this
campaign's own pre-Phase-1 baseline and to every intermediate phase's own documented screenshot.

## The Four Hard Rules (design doc Section 1.3) — restated, with plain YES/NO + evidence

1. **"`gte_core.a` compiles and links standalone with literally zero editor code, zero ImGui, zero
   debug-only feature code, zero SDL headers in its own translation units — checked mechanically."**
   **NO — partially true, not literally true.** `gte_core.a` genuinely has zero SDL/ImGui at both the
   `#include` level and the real-exported-symbol level (Phase 14's archive-content scan test still
   passes; the fresh standalone probe run above proves it). But it is NOT true that `gte_core.a` has
   "zero debug-only feature code" in the deeper "no unresolved symbol only gte_editor defines" sense:
   `RenderSystem.cpp.obj` (inside `gte_core.a`) carries a real, unresolved reference to
   `gte::RecordFrameDebuggerDraws()` (defined only in `gte_editor.a`'s
   `FrameDebuggerDrawRecording.cpp`), confirmed via an actual link attempt this phase (see above).
   `NetworkServer.cpp`/`NetworkRoutes.cpp`/`.h` also directly `#include` `Editor/Logger.h` and call
   `Logger::Query()`/`Clear()`/`EntryCount()`/`IsEnabled()`/`LatestEntryId()` — real symbols only
   `gte_editor.a` defines. Both gaps are pre-existing (since Phase 3), already disclosed (Phase 3/7/17),
   and never assigned to any phase to close.

2. **"`gte_editor.a` may depend on `gte_core.a`. Never the reverse... AND true in the weaker sense of
   'no unresolved external symbol only gte_editor.a defines'."**
   **NO, for the identical two reasons as Rule 1's second half.** The `#include`/
   `target_link_libraries()` DIRECTION is genuinely, mechanically one-way and correct (confirmed by
   reading the final `CMakeLists.txt` directly) — but the stronger "no unresolved symbol" clause the
   design doc itself calls "the actual, currently-real gap this design has to close first" is NOT
   closed; the same two symbol dependencies above are real, live violations of it today.

3. **"A thin Player-build host... can link `gte_core.a` alone and get a running, renderable engine."**
   **NO, confirmed empirically this phase.** Any executable that references `RenderSystem::Draw()` —
   which is to say, ANY renderable engine, since `RenderSystem` is the one, required middleman between
   ECS and `Renderer` (`AGENTS.md`'s own "Clean Architecture" rule) — fails to link against
   `gte_core.a` alone with a real `undefined reference to gte::RecordFrameDebuggerDraws(...)` linker
   error. This is not a theoretical risk; it was directly, mechanically reproduced.

4. **"`gte_editor.a` is unconditionally configured every single time this repository is configured —
   there is no CMake option... anywhere that skips building it."**
   **YES, with one explicitly, deliberately sanctioned exception.** The one, and only, CMake switch
   that skips `gte_editor` is `GTE_CORE_STANDALONE_PROBE_ONLY` (Phase 18), default `OFF`, documented
   "never set manually for normal development." Read completely literally, this DOES contradict Rule
   4's absolute wording — but the design doc's own Section 9/Locked Design Decision #5 explicitly
   calls for exactly this one switch to exist, naming it "the ONLY place in the whole project where
   `gte_editor` is intentionally not configured" — i.e., the design doc pre-authorizes this specific,
   narrow exception as part of satisfying its OWN Rule 2 verification requirement. For this repo's
   real, normal build (`cmake -S . -B build`, what every developer and this repo's own executable
   actually use), `gte_editor` is unconditionally built, every time, with no flag to skip it — that
   part of Rule 4's spirit is fully, genuinely satisfied.

**Honest summary**: 1 of 4 Hard Rules (Rule 4) is fully satisfied. The other 3 all trace back to
exactly the SAME two pre-existing, already-disclosed symbol dependencies (`RecordFrameDebuggerDraws`,
`Logger::Query`/`Clear`/etc.) that Phase 3 first found and flagged, Phase 7 re-confirmed still open,
and Phase 17 re-confirmed as "permanent... with no further phase in this campaign's roadmap planned to
remove them" — this phase's own contribution is turning that prior "confirmed by code reading" claim
into a mechanically-reproduced, empirical fact (a real linker error), and discovering that the
design's own verification mechanism (the archive-only standalone probe) is structurally incapable of
ever catching this specific class of violation on its own, since doing so would require an actual
executable link target — which only a Player Build Host (explicitly out of scope) could ever be.

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1** — mechanism deviation (PE-parser instead of process-launch), user-steered, documented above.
2. **PHASE2** — accepted, temporary `GTE_ENABLE_EDITOR=OFF` link break, user-signed-off.
3. **PHASE3** — discovered, disclosed, UN-CLOSED gap (`NetworkServer.cpp`/`NetworkRoutes.cpp` real `Logger` dependency) — this is the single most consequential deviation in the whole campaign; it directly drives the Four Hard Rules' honest "mostly NO" verdict above.
4. **PHASE4** — none of substance; one design fork (hook vs. no-hook) resolved by reading real code, not guessed.
5. **PHASE5** — 3 of 4 originally-sketched Bucket B interfaces turned out unnecessary (already answered by `IEditorLayer*`).
6. **PHASE6** — `ISceneIOCapability` needed a third method (`DefaultScenePath()`) the original design missed.
7. **PHASE7** — zero code changes required at all (re-confirmed PHASE5's finding independently).
8. **PHASE8** — a real, additional code fix (the `editorAvailable` bool field) beyond pure dead-branch deletion, resolved via `ask_questions`.
9. **PHASE9** — a real static-archive link-order problem (`$<LINK_GROUP:RESCAN,...>`) not anticipated by the strategy doc.
10. **PHASE12** — two deviations (reference-alias members; deferred `RenderPipeline` instances), both resolved via `ask_questions`, user deferred.
11. **PHASE13** — one genuinely new wiring shape (`SetPresentImGuiRecorder`) plus one necessary struct relocation (`GpuDrivenBatchDebugInfo`) neither anticipated in the original text.
12. **PHASE14** — the phase's own plan text was factually incomplete about scope (`SdlContext` nested inside `Application`, `EventTranslator`/`SdlMemoryTracker` also SDL-dependent, `ProfilingClock.cpp` still SDL-backed) — all discovered and fixed live; Phase 1's own regression test could never flip as originally assumed, replaced with a mechanically-true sibling test instead.
13. **PHASE17** — a stale Phase 9 prediction corrected honestly rather than left wrong (the `$<LINK_GROUP:RESCAN,...>` wrinkle is now understood to be PERMANENT, not temporary).
14. **PHASE18** — the plan's own suggested `add_subdirectory()` probe mechanism genuinely did not work (hardcoded `CMAKE_SOURCE_DIR` in `cmake/Fetch*.cmake`), replaced with a nested-`cmake`-invocation meta-project instead.
15. **PHASE19 (this phase)** — the deliberate-violation-then-revert test was performed exactly as instructed AND extended with an additional, empirical, throwaway link test (never committed) that discovered the probe's own structural blind spot described above — a genuine, new finding beyond what the phase's own literal text anticipated, documented here rather than silently left as "probe passes, all clear."

## What remains genuinely open (honest, not silently dropped)

- **The two pre-existing `gte_core → gte_editor`-only-symbol dependencies** (`RecordFrameDebuggerDraws`,
  `Logger::Query`/`Clear`/`EntryCount`/`IsEnabled`/`LatestEntryId`) are real, live, and were never
  closed by any of the 19 phases. Closing them for real needs either (a) relocating the Frame Debugger
  per-draw recording call into `Core`'s own existing `IEditorLayer*` hook mechanism (Locked Design
  Decision #8's own Bucket-1 pattern, which this exact call site should arguably have used from the
  start instead of a bare declared-elsewhere free function), or (b) a new Bucket-B-style opaque
  log-query capability for the `/get_logs`/`/clear_logs` routes. Neither is a small change; both are
  legitimate, scoped follow-up work for a future campaign, not something this closeout phase should
  attempt as a side effect of verification.
- **The standalone-core probe cannot, by its own static-archive-only nature, ever catch an
  unresolved-symbol-style violation** — only a real executable link (i.e., some form of Player Build
  Host) can. This is a structural property of this campaign's own chosen verification mechanism
  (Locked Design Decision #5), not a bug in Phase 18's implementation.
- **The Player Build Pipeline (design doc Section 8) remains explicitly, permanently OUT OF SCOPE** —
  restated here one final time, exactly as every phase's own report already insists: no
  `<ProjectName>.exe` generation, no per-project build system, no Player host template beyond what
  Phase 18's standalone-core probe already needs. Nothing in this campaign builds it, and nothing in
  this campaign should be read as having secretly started it.
- No real CI pipeline exists for this repo (unchanged, Locked Design Decision #5) — the standalone-core
  probe remains a manually-invocable local tool, exactly as designed.
- `GTE_ENABLE_PROJECT_PANEL` remains untouched, orthogonal, now gating `gte_editor`'s own source list
  instead of `gte_core`'s — zero semantic change, as designed.

## Final structural facts (confirmed this phase, not merely inherited from prior reports)

- Two real, separately-linked static libraries exist: `gte_core.a`, `gte_editor.a` — one-way
  dependency, `gte_editor → gte_core`, confirmed via `CMakeLists.txt` and the standalone probe.
  `Core`/`EditorHost` are the real public contract/composition root; `Application` no longer exists.
  The executable is `GreatTamanaEditor.exe`.
  `GTE_ENABLE_EDITOR` exists nowhere in the codebase as a real preprocessor directive or CMake
  conditional.
- Full clean build: 497/497 steps, zero errors. Full `ctest`: 1773 total, 1771 passed (100% of
  executed), 2 legitimate environment-gated skips, zero failures.
- Live HTTP smoke test: every endpoint this campaign touched behaves identically to its own
  pre-Phase-1 baseline.

`editor-core-separation-1` is complete, its real, substantial structural deliverable (two genuine,
correctly one-way-dependent CMake targets; `Core`/`EditorHost` as the new public contract/composition
root; the executable renamed) has shipped and is verified working end-to-end — but this closeout
report explicitly, deliberately does **NOT** claim the design doc's own Four Hard Rules are all fully
met today. Three of the four trace to the same narrow, pre-existing, already-disclosed gap. Ready to
merge, with that gap recorded here as an honest, load-bearing "what remains open" item for whoever
picks up the follow-up work.
