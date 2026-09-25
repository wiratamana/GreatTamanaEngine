# PHASE1 — Extract Presentation-Formatting Helpers — COMPLETION REPORT

**Status: DONE.** No deviation from the phase plan (`PHASE1_EXTRACT_FORMATTING_AND_LABEL_HELPERS.md`)
or the parent `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions #5/#12 — this
was a pure, mechanical relocation plus two brand-new additive functions, exactly
as scoped.

## What was done

1. **New file pair**: `src/Renderer/RenderGraph/RenderGraphSnapshotFormatting.h`
   and `.cpp` — `gte_core`-tier, ImGui-free, `namespace gte::rg`. Contains:
   - `FormatGpuTiming(const GpuTimingSample&)` — relocated verbatim (identical
     body) from `RenderGraphPanel.cpp`'s anonymous namespace.
   - `JoinNames(const std::vector<std::string>&)` — relocated verbatim.
   - `ResolvePassNameAtSurvivingIndex(const RenderGraphSnapshot&, std::int32_t)`
     — relocated (identical body) and RENAMED from `PassNameAtSurvivingIndex()`,
     exactly as the phase plan specified.
   - `ToString(ResourceKind) noexcept` — brand NEW. Confirmed the real, current
     enumerator list directly from `RenderGraphTypes.h` before writing the
     switch: `Texture`, `Buffer`, `VolumeTexture` (exactly 3, no more, no
     fewer) — mirrors `RenderGraphTypes.cpp`'s own `ToString()` precedent: a
     plain switch, deliberately NO `default:` case.
   - `ToString(ViewScope) noexcept` — brand NEW. Confirmed enumerator list:
     `Shared`, `GameView`, `SceneView` (exactly 3) — same "no default: case"
     discipline.

2. **`RenderGraphPanel.cpp`** — narrow, surgical diff only:
   - Added one new `#include "../../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"`.
   - Deleted the three anonymous-namespace function bodies (and their
     preceding doc comments) entirely.
   - Updated every call site to the qualified `rg::` names:
     `rg::FormatGpuTiming(...)`, `rg::JoinNames(...)` (2 call sites),
     `rg::ResolvePassNameAtSurvivingIndex(...)` (2 call sites) — matching the
     file's own pre-existing `rg::` qualification convention (no new
     `using namespace` introduced).
   - Nothing else in the file was touched — no reordering, no reformatting,
     no rewording of unrelated comments. `git diff` was inspected by hand and
     confirmed to contain only: 1 new include, 3 deleted function bodies (plus
     their doc comments), and the 5 call-site qualification edits above (2
     small blank-line adjustments were needed purely as a mechanical side
     effect of removing the deleted blocks so no double-blank-line/missing-
     blank-line artifact was left behind).

3. **New Tier-1 test file**:
   `tests/Renderer/RenderGraph/RenderGraphSnapshotFormattingTests.cpp` — 13
   new tests, mirroring `RenderGraphSnapshotTests.cpp`'s own
   include/namespace/`TEST()` registration style exactly. Covers every case
   the phase plan's "Minimum cases" list asked for:
   - `FormatGpuTiming`: `Present` (`"3.14 ms"`), `Unsupported`, `Absent`.
   - `JoinNames`: empty vector (`"-"`), one name, two names (comma-joined),
     an empty-string name (`"(unnamed)"`).
   - `ResolvePassNameAtSurvivingIndex`: negative index, out-of-range index,
     valid index (real name), valid index with an empty name (`"(unnamed)"`).
   - `ToString(ResourceKind)`: one assertion per current enumerator
     (`Texture`/`Buffer`/`VolumeTexture`), each distinct/non-null/non-empty.
   - `ToString(ViewScope)`: one assertion per current enumerator
     (`Shared`/`GameView`/`SceneView`), same shape.

4. **CMake wiring**:
   - Root `CMakeLists.txt`: `search_in_dir` re-confirmed
     `RenderGraphSnapshot.cpp` was still at line 665 (unchanged since the
     phase file was written — no concurrent campaign had shifted it). Added
     the two new `gte_core` source entries immediately after it.
   - `tests/CMakeLists.txt`: re-confirmed `RenderGraphSnapshotTests.cpp` was
     still at line 2184. Added the new test file entry immediately after it.

## Verification evidence

1. **Incremental build** (`cmake --build build`): succeeded cleanly. Only
   the expected objects recompiled/relinked
   (`RenderGraphSnapshotFormatting.cpp.obj`, `RenderGraphPanel.cpp.obj`,
   `libgte_core.a`, `libgte_editor.a`, `GreatTamanaEditor.exe`,
   `GreatTamanaEngineTests.exe`). The only warnings emitted are the
   pre-existing, unrelated MinGW static-CRT/plugin-linkage warnings this
   repo's build has always printed (confirmed identical text before and
   after this phase's changes) — zero new warnings from any file this phase
   touched.

2. **New tests run in isolation** (per Workflow Rule 1 — no full `ctest`):
   `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraphSnapshotFormattingTest.*`
   → **13/13 PASSED**, 0 failed.

3. **Live, HTTP-driven before/after screenshot comparison** — done properly,
   not just a single "after" shot:
   - `git_stash` (stashes the 3 tracked-file modifications; the new untracked
     files stay on disk but are no longer referenced by the stashed
     `CMakeLists.txt`, so the build reverts to the exact pre-phase behavior).
   - Rebuilt (`cmake --build build`) → old `RenderGraphPanel.cpp` binary.
   - `run_app_background` the reverted `GreatTamanaEditor.exe`,
     `GET /activate_tab?name=Render%20Graph`, `GET /get_swapchain` → **BEFORE**
     screenshot captured (`load_image`-viewable inline result), showing the
     "Render Graph" panel's GPU-Driven Batches / Plugin Render Features /
     Offscreen Regime pass+resource tables, with GPU Time column values like
     `"0.19 ms"`, `"2.20 ms"`, Reads column values like
     `"AtmosphereTransmittanceLut, AtmosphereMultiScatteringLut"` and `"-"`.
   - `stop_app_background`, `git_stash_pop` (restores this phase's real
     changes), rebuilt again → new binary.
   - `run_app_background`, same two HTTP calls → **AFTER** screenshot
     captured. Layout, column headers, section order, row content, and every
     formatted string shape (`"X.XX ms"`, comma-joined Reads/Writes lists,
     `"-"` placeholders, `"(unnamed)"` handling) are pixel-for-pixel identical
     to the BEFORE screenshot in every respect except the live GPU-timing
     numbers themselves, which are expected to vary slightly frame-to-frame
     (real, live GPU timestamp measurements, e.g. `"2.18 ms"` vs. `"2.20 ms"`
     for `AtmosphereMultiScatteringLut` — this is normal measurement jitter,
     not a regression; both are correctly formatted by the SAME
     `rg::FormatGpuTiming()` now living in the new shared file).
   - `GET /get_logs?limit=50` after the AFTER run: only the same
     pre-existing warnings (plugin static-CRT-linkage notice, "2 loaded
     plugins implement IRenderFeatureModule_v1", 3 GPU-timing-slot-budget-
     exhausted warnings for unrelated plugin passes) — identical in content
     and count to what a stock run already produces; zero new
     warnings/errors introduced by this phase.
   - `stop_app_background` after each run.

4. **`git_status` re-confirmed immediately before this report/commit**: the
   working tree shows exactly the files this phase's plan says it may touch:
   `CMakeLists.txt` (modified), `src/Editor/Panels/RenderGraphPanel.cpp`
   (modified), `tests/CMakeLists.txt` (modified), plus 3 new untracked files
   (`RenderGraphSnapshotFormatting.h/.cpp`,
   `RenderGraphSnapshotFormattingTests.cpp`) and the pre-existing untracked
   `task_manager/editor-core-separation-7/` strategy folder. No other file
   was touched.

## What this phase deliberately did NOT do (unchanged from the plan)

- Did not create `RenderGraphMetadata` or any JSON conversion (PHASE2).
- Did not change `RenderGraphPanel::Build()`'s own signature, call order, or
  section layout in any way.
- Did not touch `RenderPassGroupRegistry`/tag-label resolution.
- Did not run a full clean build or full `ctest` regression (reserved for
  PHASE5 per Workflow Rule 1).

## Ambiguities encountered

None. Every fact needed (the real `ResourceKind`/`ViewScope` enumerator
lists, the exact current CMake line numbers, the existing `ToString()`
switch-without-`default:` style) was directly confirmed by reading the real
source before writing any code, exactly as the phase file instructed — no
`ask_questions` call was needed.
