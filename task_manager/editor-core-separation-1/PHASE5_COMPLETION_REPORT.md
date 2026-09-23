# PHASE5 — COMPLETION REPORT: Bucket B, Part 1 — Design and Declare Small Capability Interfaces

## Parent
`PHASE0_MASTER_STRATEGY.md` (Locked Design Decision #2 — many small
interfaces, never one big one; Locked Design Decision #8 — `IEditorLayer`/
`FrameDebuggerCaptureContext*` are already-designed, out-of-scope hooks),
plus the original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md`,
`PHASE2_COMPLETION_REPORT.md`, `PHASE3_COMPLETION_REPORT.md`, and
`PHASE4_COMPLETION_REPORT.md` (the four prior completion reports in this
campaign folder) read in full for continuation clues.

## Status: DONE

## Headline finding — ONE interface declared, not four

PHASE5's own strategy file sketched four candidate interfaces
(`ISceneIOCapability`, `IAssetImportCapability`, `IEditorUiCapability`,
`IGpuDrivenBatchTestCapability`) and explicitly told me to "confirm during
execution" whether each candidate call site genuinely needs a Bucket-B-style
capability check at all. I read every real, current file named in the
strategy's own Step 2 list (`EngineCommandDispatch.cpp`, `NetworkServer.cpp`,
`Game.h`, `AssetImportCommandBridge.h`, `EditorUiCommandBridge.h`,
`GpuDrivenBatchTestSpawner.h`) plus their real CORE-side call sites
(`Application.cpp`), and confirmed: **only ONE of the four candidates is a
genuinely new Bucket B gap.** The other three are already fully answered by
the pre-existing, ALREADY-DESIGNED `IEditorLayer*` opaque-pointer mechanism
(Locked Design Decision #8), which this phase must not redesign or duplicate.
Declaring redundant interfaces for them would itself violate that same locked
decision by creating a second mechanism to answer a question `IEditorLayer`
already answers.

### The one real gap: `ISceneIOCapability`

`src/Application/EngineCommandDispatch.cpp`'s `EngineCommandKind::SaveScene`/
`LoadScene` handling still answers "can I save/load a scene" with a literal
`#if GTE_ENABLE_EDITOR` / `#else` / `#endif` block, calling
`src/Editor/SceneIO.h`'s real `SaveScene(Game&, const std::filesystem::path&)`/
`LoadScene(Game&, Renderer&, const std::filesystem::path&)` free functions
directly inside the `#if` branch, and setting
`result.saveScene.editorAvailable = false` (plus the hardcoded
`"...requires the Editor module (GTE_ENABLE_EDITOR is OFF in this build)"`
message) in the `#else` branch. This is a real, live compile-time gate with no
existing opaque-pointer/interface answer anywhere — a genuine new Bucket B
gap.

`src/Network/NetworkServer.cpp`'s `POST /save_scene`/`POST /load_scene` route
handlers do **not** have their own macro at all (confirmed via
`search_in_dir` for `"save_scene"`/`"editorAvailable"` scoped to
`src/Network/`) — they only ever read the plain
`SaveSceneOutcome`/`LoadSceneOutcome::editorAvailable`/`errorMessage` fields
`EngineCommandDispatch.cpp` already produces, via the existing
`EngineCommandBridge`. Once PHASE6 converts `EngineCommandDispatch.cpp`'s own
`#if` to a runtime null-check against `ISceneIOCapability*`, those two output
fields keep the exact same shape/meaning, so `NetworkServer.cpp`'s own
HTTP-status-mapping code needs **zero changes** for this interface's sake —
confirmed by reading the real route handlers in full (`NetworkServer.cpp`
lines ~868-955).

### The three candidates that turned out NOT to be new gaps

I traced each of the three remaining candidates all the way to their real
CORE-side call site (`Application::Run()`, `Application.cpp` lines
1223-1339) and confirmed every one of them is called **unconditionally**
(no `#if GTE_ENABLE_EDITOR` anywhere on the call site itself), always through
the same pre-existing `std::unique_ptr<IEditorLayer> m_editorLayer` member —
which is never null itself (it holds either the real `ImGuiEditorLayer` or
the inert `NullEditorLayer`, selected at compile/link time by which `.cpp`
got built) and already has a complete, correct "not available in this build"
answer for every one of them, confirmed by reading `NullEditorLayer.cpp` in
full:

- **`EditorUiCommandBridge.h` (`ActivateTab` command)** →
  `m_editorLayer->ActivateTab(uiRequest->activateTab.tabName)`
  (`Application.cpp:1239`) → `NullEditorLayer::ActivateTab()` returns a
  default-constructed `TabActivationResult{}` (`tabExists == false`,
  `success == false`) — already a complete, working "unavailable" answer.
- **`AssetImportCommandBridge.h`** →
  `m_editorLayer->ImportExternalAssetIntoProject(...)`
  (`Application.cpp:1322`) → `NullEditorLayer::ImportExternalAssetIntoProject()`
  explicitly sets `result.projectAvailable = false` and returns — already a
  complete, working "unavailable" answer, and in fact the EXACT shape
  `AssetImportCommandBridge.h`'s own `ImportExternalFileOutcome::projectAvailable`
  doc comment already describes.
- **`GpuDrivenBatchTestSpawner.h`** →
  `m_editorLayer->SpawnGpuDrivenTestBatch(m_game, m_renderer, ...)`
  (`Application.cpp:1233`) → `NullEditorLayer::SpawnGpuDrivenTestBatch()`
  explicitly returns `success = false` with the message
  `"GPU-driven test batch spawning is not available in this build
  (GTE_ENABLE_EDITOR is OFF)"` — already a complete, working "unavailable"
  answer. `GpuDrivenBatchTestSpawner.h`/`.cpp` themselves already live
  entirely under `src/Editor/` (confirmed — this file's own header comment
  already says "Editor-only (GTE_ENABLE_EDITOR) by explicit project-owner
  decision"), so it was never a candidate for living in `gte_core` at all;
  the only question was whether its ONE real Core-side caller
  (`Application.cpp`) needed a new capability check, and it does not.

I also re-confirmed `Game.h`/`Game.cpp` directly (`search_in_dir` for
`"GTE_ENABLE_EDITOR"`/`"not available"` scoped to `src/Game/`, zero results
either way) — this file carries no macro and no "not available" fallback of
any kind today (the only Editor-related thing in `Game.h` is the
already-out-of-scope `FrameDebuggerCaptureContext*` forward declaration, per
Locked Design Decision #8). There is nothing here for this phase to design.

**Per Locked Design Decision #2's own text** — "This rule governs ONLY the
genuinely NEW Bucket B capability gaps" — a capability question already fully
answered by the existing, out-of-scope `IEditorLayer*` hook gets no new
interface. This finding directly extends PHASE5's own anticipated
"Bucket C, not Bucket B" possibility (explicitly called out for
`GpuDrivenBatchTestSpawner.h` in the strategy file) to `EditorUiCommandBridge.h`
and `AssetImportCommandBridge.h` too, backed by the identical kind of
evidence (a real, direct read of the current call site, not a guess).

**Consequence for PHASE7**: PHASE7's own plan currently assumes new
`IEditorUiCapability`/`IAssetImportCapability` adapters need to be built and
wired. Per this finding, PHASE7 should expect to find **zero** remaining
`#if GTE_ENABLE_EDITOR` at the real call sites for these three items when it
re-runs its own `search_in_dir` sweep, and can document them as
already-resolved (no conversion needed) rather than building unnecessary
adapters — this is flagged here loudly, per Universal Rule 9, exactly the way
PHASE3 flagged its own "Discovered gap" and PHASE4 flagged its own
"SdlMemoryTracker.h needs no change" finding for a future phase to consume.

## What I did

1. Read every real, current file this phase's own Step 2 named
   (`EngineCommandDispatch.cpp`/`.h`, `NetworkServer.cpp`, `Game.h`,
   `AssetImportCommandBridge.h`, `EditorUiCommandBridge.h`,
   `GpuDrivenBatchTestSpawner.h`, `NullEditorLayer.cpp`, `SceneIO.h`) plus
   `Application.cpp`'s/`Application.h`'s real current shape, via
   `read_file`/`search_in_dir`, before writing anything — confirming the
   "Headline finding" above.
2. **NEW `src/Core/EditorCapabilities.h`** — declares exactly one interface,
   `ISceneIOCapability` (`SaveScene(Game&, const std::filesystem::path&,
   std::string& outErrorMessage)` / `LoadScene(Game&, Renderer&, const
   std::filesystem::path&, std::string& outErrorMessage)`), mirroring
   `Editor/SceneIO.h`'s real free-function signatures exactly (confirmed by
   reading that header in full first). The header's own top-of-file comment
   documents the full "why only one, not four" reasoning above, so a future
   reader of this file alone (without re-reading this report) understands
   the complete picture.
3. **`CMakeLists.txt`** — registered the new header in `gte_core`'s
   unconditional source list (header-only, no `.cpp` — every method is pure
   virtual), right after `src/Core/LogSink.cpp`.
4. **`src/Application/Application.h`** — added the temporary wiring point
   (per this phase's own "Files Touched" guidance: "likely `Application.h`
   for now, since `Core`/`EditorHost` don't exist until later phases"):
   - `#include "../Core/EditorCapabilities.h"`.
   - A public setter, `void SetSceneIOCapability(ISceneIOCapability*
     capability) noexcept`, documented as PHASE6's own future wiring call
     (never called by this phase itself).
   - A private, default-`nullptr` member, `ISceneIOCapability*
     m_sceneIOCapability = nullptr;`.
   Neither the setter nor the member is read/written anywhere else yet — per
   this phase's own explicit "do NOT touch any real call site's `#if
   GTE_ENABLE_EDITOR` block yet" instruction, `EngineCommandDispatch.cpp`'s
   own macro is completely untouched, and nothing calls the new setter. This
   is intentional, temporary dead wiring, exactly like PHASE2's own
   `RecordFrameDebuggerDraws()` declaration existed for one phase before a
   later phase gave it a real caller.
5. Did **not** touch any other file — `EngineCommandDispatch.cpp/.h`,
   `NetworkServer.cpp`, `Game.h`, `AssetImportCommandBridge.h`,
   `EditorUiCommandBridge.h`, `GpuDrivenBatchTestSpawner.h` are all
   byte-for-byte unchanged, confirmed via `git_status` before committing.

## Where each interface's nullable-pointer wiring point lives, and why (Definition of Done requirement)

**`ISceneIOCapability*`** — lives as a private member,
`Application::m_sceneIOCapability` (`src/Application/Application.h`), set via
the new public `Application::SetSceneIOCapability()` method. Chosen because:

- `Application` is today's real composition root (per the design doc's own
  Section 2.1) and the ONLY object that currently constructs/owns
  `EngineCommandDispatch.cpp`'s real caller context (it calls
  `ExecuteEngineCommand(m_game, m_renderer, *request)` directly,
  `Application.cpp:1162`) — the same object PHASE6 will need to thread the
  capability pointer through into `ExecuteEngineCommand()`'s own signature.
- This mirrors the exact "temporary home in `Application`, relocated without
  redesign later" precedent this phase's own strategy file explicitly
  calls for, and the exact same precedent `m_editorLayer`/
  `m_currentFrameDebuggerCaptureForOffscreenPipeline` already establish on
  this same class — a nullable pointer/interface member, owned by whoever
  currently composes the app, consulted by null-check rather than by macro.
- `Core`/`EditorHost` do not exist yet (they are PHASE12/PHASE15's own
  deliverables) — per this phase's own "Files Touched" section, adding the
  member here now, then relocating it later without needing to redesign
  `ISceneIOCapability` itself, is the explicitly sanctioned sequencing.

No other new capability got a wiring point in this phase — see the
"Headline finding" section above for the full, evidence-based reasoning for
each of the three candidates that turned out not to need one.

## Compile-check / smoke-check results

**Incremental compile check only, per campaign policy** — no full clean
build, no full `ctest` regression pass (not required until Phase 9/14/19).

- `cmake --build build --target gte_core` — **succeeded cleanly** (2 build
  steps: `Application.cpp.obj` recompiled, `libgte_core.a` relinked — only
  the pre-existing, unrelated `third_party/ktx` `git describe` warning, same
  as every prior phase's own report).
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**
  (full executable relinked, every `.spv` shader staged as usual).
- **Extra, beyond-minimum verification**: `cmake --build build-editor-off
  --target gte_core` (the `GTE_ENABLE_EDITOR=OFF` configuration) —
  **succeeded cleanly** too (34 build steps, confirming
  `Core/EditorCapabilities.h`, being unconditional in `gte_core`'s own
  source list, compiles fine in BOTH configurations, and that
  `Application.h`'s new `#include`/member/setter introduce no OFF-mode
  regression beyond the pre-existing, already-documented PHASE2 executable
  -link break).
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`,
  then via `gte_send_request`:
  - `GET /get_swapchain` — screenshot confirmed the Editor renders normally
    (Hierarchy/Scene/Game/Inspector/Project panels all visible and correct).
  - `GET /get_logs?min_level=Warning&limit=50` — `count: 0`, no new
    warnings/errors.
  - `POST /save_scene` (`{"path":""}`) — `200`, `success: true`, confirming
    the EXISTING scene-save path (still macro-gated, untouched by this
    phase) keeps working exactly as before.
  - `stop_app_background`'d the process when done.

No `bug_report` was filed — no tool malfunctioned during this phase.

## Definition of Done — checklist

- [x] One small interface per real, genuinely-new capability exists,
      matching what the real call sites actually need (verified by reading
      them, not guessed) — `ISceneIOCapability`, the ONE real gap found.
- [x] `PHASE5_COMPLETION_REPORT.md` documents, for each interface (and, just
      as importantly, for each candidate that turned out NOT to need one),
      exactly where its nullable-pointer wiring point lives and why — see
      the dedicated section above.
- [x] Git commit (see commit that follows this report).

## Out of Scope (confirmed, unchanged)

- Did not convert any real call site's `#if GTE_ENABLE_EDITOR` block —
  `EngineCommandDispatch.cpp`'s scene save/load macro is completely
  untouched; that is PHASE6's job.
- Did not touch `IEditorLayer`/`FrameDebuggerCaptureContext*` themselves, and
  did not redesign/fragment `IEditorLayer` — confirmed via `git_status`
  (`src/Editor/EditorLayer.h` not among the modified files).
- Did not create the real `EditorSceneIOCapability` adapter implementation —
  that is PHASE6's job (`src/Editor/EditorSceneIOCapability.h`/`.cpp`).
- No CMake target surgery (`gte_editor` target, etc.) — still Phase 9's job.

## Files touched

- NEW: `src/Core/EditorCapabilities.h`
- MODIFIED: `CMakeLists.txt` (registered the new header)
- MODIFIED: `src/Application/Application.h` (new include, new public setter,
  new private nullable member — no `.cpp` change needed, the setter is a
  one-line inline method)
- NEW: `task_manager/editor-core-separation-1/PHASE5_COMPLETION_REPORT.md`
  (this file)
