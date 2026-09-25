# PHASE3 — Editor Panel Becomes Data-Driven + Real "Export DOT"

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Design Decisions #13 and #14). Also read `PHASE1_COMPLETION_REPORT.md` and
`PHASE2_COMPLETION_REPORT.md` before starting. Use `ask_questions` for any
genuine ambiguity.

## Step 1: The Goal

Refactor `RenderGraphPanel::Build()` so it builds ONE `rg::RenderGraphMetadata`
per frame (or reuses a frozen one, exactly preserving the existing Pause
semantics) and draws its ImGui tables **from that metadata object**, never
again touching `RenderGraphSnapshot`/`GpuDrivenBatchDebugInfo`/
`RenderFeatureDebugEntry` fields directly inside the drawing code. Implement
the real "Export DOT" button (disabled since the original Render Graph
campaign's Phase 8), consuming the exact same metadata object. The panel's
own rendered ImGui output must remain visually IDENTICAL to before this
phase (same columns, same text, same layout) — this phase changes WHERE the
panel gets its data from, not what a human sees.

## Step 2: The Situation

Confirmed by direct read, `src/Editor/Panels/RenderGraphPanel.h` (current):

```cpp
void Build(EditorContext& ctx, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries);

private:
    bool m_paused = false;
    rg::RenderGraphSnapshot m_frozenOffscreenSnapshot;
    rg::RenderGraphSnapshot m_frozenPresentSnapshot;
    std::vector<GpuDrivenBatchDebugInfo> m_frozenGpuDrivenBatchDebugInfo;
```

Confirmed, `RenderGraphPanel.cpp`'s current `Build()` body (Step 2 of the
Investigation/PHASE0 already quotes the file in full) — the Pause state
machine is: `wasPaused = m_paused;` -> `ImGui::Checkbox("Pause", &m_paused)` ->
`if (m_paused && !wasPaused) { /* capture frozen copies */ }` -> every
section below reads either the LIVE values or the FROZEN ones depending on
`m_paused`'s CURRENT value. This exact state machine must be preserved
byte-for-byte, just applied to ONE `rg::RenderGraphMetadata` field instead of
three separate frozen fields.

`Build()`'s own public SIGNATURE (its three trailing parameters) must NOT
change in this phase — `ImGuiEditorLayer.cpp`'s call site
(`m_renderGraphPanel.Build(m_ctx, renderGraph, gpuDrivenBatchDebugInfo, renderFeatureEntries);`)
and `NullEditorLayer.cpp`'s matching stub both already exist and are
correct — this phase only changes what happens INSIDE `Build()`'s own body,
never its call sites (there is no reason to touch either caller; changing
the signature here would be pure, unjustified churn).

**Where "Export DOT" support code lives (PHASE0_MASTER_STRATEGY.md's Locked
Design Decision #14)**: `RenderGraphDotExport.h/.cpp` (Step 3.4 below) lives
under `src/Editor/` — compiled into `gte_editor`, alongside
`RenderGraphPanel.h/.cpp` itself — NOT under `src/Renderer/RenderGraph/`
(`gte_core`) next to `RenderGraphMetadata.h/.cpp`. This is a real, evidence-
backed placement, not a stylistic preference: this repo has an established,
repeatedly-used precedent for "a pure-or-near-pure, non-ImGui,
Editor-button-triggered debug/export tool that consumes `rg::`/`Renderer`
types but has no consumer outside the Editor" —
`src/Editor/ComputeBlurValidation.h/.cpp`, `src/Editor/GBufferValidation.h/.cpp`,
`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`, and
`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp` are all
real, current examples, and all four live under `src/Editor/` in `gte_editor`.
`RenderGraphDotExport.h/.cpp` has exactly one consumer, forever, by this
campaign's own scope (`RenderGraphPanel::Build()`'s "Export DOT" button) —
no `gte_core`-tier code ever calls it (confirmed: PHASE4's own "What this
phase does NOT do" section). `RenderGraphMetadata.h/.cpp` itself stays in
`gte_core` (PHASE4's `FrameCaptureBridge` needs it there) — only the DOT
exporter moves to the Editor tier.

## Step 3: The Plan

### Step 3.1 — `RenderGraphPanel.h` changes

Replace the three frozen fields with ONE:

```cpp
private:
    bool m_paused = false;
    rg::RenderGraphMetadata m_frozenMetadata; // replaces m_frozenOffscreenSnapshot/m_frozenPresentSnapshot/m_frozenGpuDrivenBatchDebugInfo - genuine simplification, not just a rename.
```

`#include "../../Renderer/RenderGraph/RenderGraphMetadata.h"` replaces the
existing `#include "../../Renderer/RenderGraph/RenderGraphSnapshot.h"` (which
`RenderGraphMetadata.h` itself already transitively includes — confirm this
via `read_file` on PHASE2's own header before removing the direct include,
so this file still compiles with every type name it currently uses still
visible).

### Step 3.2 — `RenderGraphPanel.cpp` changes

`Build()`'s new body shape:

```cpp
void RenderGraphPanel::Build(EditorContext& /*ctx*/, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries)
{
    ImGui::Begin("Render Graph");

    const bool wasPaused = m_paused;
    ImGui::Checkbox("Pause", &m_paused);
    ImGui::SameLine();
    ImGui::TextDisabled("(freezes only this panel's own display - the graph keeps running underneath)");

    if (m_paused && !wasPaused) {
        m_frozenMetadata = rg::BuildRenderGraphMetadata(
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
            gpuDrivenBatchDebugInfo, renderFeatureEntries);
    }

    const rg::RenderGraphMetadata liveMetadata = m_paused ? rg::RenderGraphMetadata{} : rg::BuildRenderGraphMetadata(
        renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
        renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
        gpuDrivenBatchDebugInfo, renderFeatureEntries);
    const rg::RenderGraphMetadata& metadata = m_paused ? m_frozenMetadata : liveMetadata;

    BuildGpuDrivenBatchesSection(metadata.gpuDrivenBatches);
    ImGui::Spacing();
    BuildPluginRenderFeaturesSection(metadata.renderFeatures);
    ImGui::Spacing();
    BuildRegimeSection("Offscreen Regime (Game View + Scene View)", "Offscreen", metadata.offscreenRegime);
    ImGui::Spacing();
    BuildRegimeSection("Pipelined Regime (Present)", "Present", metadata.presentRegime);

    ImGui::Spacing();
    ImGui::SeparatorText("Export");
    if (ImGui::Button("Export DOT")) {
        const std::string path = ExportRenderGraphDotToFile(metadata); // Step 3.4 below.
        GTE_LOG_INFO("RenderGraphPanel", "Exported Render Graph DOT file to: %s", path.c_str());
    }

    ImGui::End();
}
```

(Illustrative — write the REAL version; the important, load-bearing shape
is: build `liveMetadata` UNCONDITIONALLY only when NOT paused (avoid the
wasted `BuildRenderGraphMetadata()` call entirely while paused — this is a
real, cheap, correct optimization the sketch above's `m_paused ? rg::RenderGraphMetadata{} : ...`
ternary achieves by short-circuiting the expensive branch, but confirm this
reads cleanly in real code — an `if/else` may be clearer than the nested
ternary shown here; either is acceptable as long as `BuildRenderGraphMetadata()`
is called AT MOST once per `Build()` invocation, either into `m_frozenMetadata`
on the pause-transition frame, or into a local for display, never both in
the same call). `BuildGpuDrivenBatchesSection()`/`BuildPluginRenderFeaturesSection()`/
`BuildRegimeSection()` all change their own PARAMETER TYPES (from
`std::vector<GpuDrivenBatchDebugInfo>&`/`std::vector<RenderFeatureDebugEntry>&`/
`rg::RenderGraphSnapshot&` to the metadata equivalents:
`std::vector<GpuDrivenBatchDebugInfo>&` stays THE SAME for
`BuildGpuDrivenBatchesSection` since `RenderGraphMetadata::gpuDrivenBatches`
is that exact same type reused directly — Locked Design Decision #6 pays off
here: this section's own drawing code needs ZERO changes, only its call
site's data SOURCE changed. Same for `BuildPluginRenderFeaturesSection`.
`BuildRegimeSection`/`BuildPassTable`/`BuildResourceTable`/`BuildPassRow` DO
need real changes — they currently read `rg::RenderGraphSnapshot`/
`rg::RenderGraphPassSnapshot`/`rg::RenderGraphResourceSnapshot` fields
directly (raw enums, raw `GpuTimingSample`, raw indices); they must be
rewritten to read `rg::RenderGraphRegimeMetadata`/`rg::RenderGraphPassMetadata`/
`rg::RenderGraphResourceMetadata` fields instead (already-resolved strings —
`pass.kind`/`pass.category`/etc. are now `std::string`, printed with
`ImGui::TextUnformatted(pass.kind.c_str())` instead of
`ImGui::Text("%s", rg::ToString(pass.kind))`; `pass.gpuTimingText` replaces
a call to `FormatGpuTiming(pass.stats.timing)` directly, etc.). Walk EVERY
existing ImGui call in `BuildPassRow()`/`BuildPassTable()`/`BuildResourceTable()`
and confirm the NEW code path produces the exact same visible text for the
exact same underlying data — this is the part of this phase most likely to
introduce a subtle visual regression if rushed; take it slowly, one ImGui
call at a time, comparing against the ORIGINAL file (read it first, keep it
open for reference) rather than reconstructing each line from memory.

`JoinNames()` is STILL needed here (PHASE1 kept it, it was never JSON-only) —
`pass.reads`/`pass.writes` are now `std::vector<RenderGraphResourceRefMetadata>`
(name+kind pairs) rather than `std::vector<std::string>`; `BuildPassRow()`
needs a small NEW local helper (or an updated `JoinNames()` overload) that
extracts just the `.name` field from each `RenderGraphResourceRefMetadata`
before joining — do not silently drop the `.kind` field's own information
from the ImGui display if the ORIGINAL table never showed per-read/write
kind text either (confirm this by re-reading the ORIGINAL `BuildPassRow()`
in Step 2's own quoted source — it only ever showed `JoinNames(pass.readNames)`/
`JoinNames(pass.writeNames)`, i.e. names only, never kinds — so the NEW code
should extract names-only here too, for a truly byte-identical visual
result; PHASE2's `kind` field on each ref exists for the JSON consumer, Phase
4, not because this ImGui table needs to start showing it).

### Step 3.3 — `#include` cleanup

`RenderGraphPanel.cpp` no longer needs a direct
`#include "../../Renderer/RenderGraph/RenderGraph.h"`'s
`rg::RenderGraphSnapshot`/enum types directly for its OWN drawing logic (it
still needs `RenderGraph.h` for `renderGraph.LastSnapshot(...)`/
`rg::ExecuteTimingMode` at the `Build()` call site itself) — add
`#include "../../Renderer/RenderGraph/RenderGraphMetadata.h"` alongside it,
plus `#include "../RenderGraphDotExport.h"` (Step 3.4 below — one directory
up from `Panels/`, since that new file lives directly under `src/Editor/`,
the exact same relative-include shape `RenderGraphPanel.cpp` already uses
for `#include "../EditorContext.h"`).
Confirm via a clean incremental build that no now-unused include triggers a
new compiler warning this repo treats as an error (check this repo's own
warning-as-error CMake flags before assuming either way).

### Step 3.4 — New files: `src/Editor/RenderGraphDotExport.h` + `.cpp`

A third pure, near-engine-free function — the real "Export DOT"
implementation — living under `src/Editor/` (compiled into `gte_editor`,
NOT `src/Renderer/RenderGraph/`/`gte_core` — see Step 2's "Where 'Export
DOT' support code lives" note above and PHASE0_MASTER_STRATEGY.md's Locked
Design Decision #14), consuming `rg::RenderGraphMetadata` (never a live
`RenderGraph&`):

```cpp
#pragma once

// editor-core-separation-7 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md) - the real
// "Export DOT" implementation the original Render Graph campaign's own
// RenderGraphPanel.cpp button tooltip has promised since Phase 8
// ("Planned for Phase 9, once this panel's own ImGui-list data model has
// proven itself"). Pure function of an already-built rg::RenderGraphMetadata -
// no live RenderGraph/file I/O inside this function itself (see
// ExportRenderGraphDotToFile() below for the one thin, file-writing wrapper
// that DOES do I/O, kept deliberately separate so this function stays
// Tier-1-testable on its own text output).
//
// Lives under src/Editor/ (gte_editor), NOT src/Renderer/RenderGraph/
// (gte_core) - PHASE0_MASTER_STRATEGY.md's Locked Design Decision #14:
// this file has exactly ONE consumer, forever, RenderGraphPanel::Build()'s
// own "Export DOT" button, mirroring src/Editor/ComputeBlurValidation.h/.cpp,
// src/Editor/GBufferValidation.h/.cpp,
// src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp, and
// src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp's own
// identical "Editor-only debug/export tool consuming rg::/Renderer types"
// precedent - all four live here too, not under Renderer/RenderGraph/.

#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <string>

namespace gte {

// Produces a complete, valid Graphviz DOT document text (a "digraph"
// wrapping one visually-distinct subgraph cluster per regime -
// "OffscreenRegime"/"PresentRegime" - each pass a node, each declared
// read/write an edge from/to a small resource node, a culled pass rendered
// with a dashed/grey node style so a human opening this in any Graphviz
// viewer can immediately see what ran vs. what got culled, mirroring this
// panel's own existing ImGui "TextDisabled for culled" convention). Pure -
// no file I/O, directly Tier-1-testable by asserting substring content
// (e.g. every pass name appears, a culled pass's node carries the expected
// style attribute).
std::string BuildRenderGraphDot(const rg::RenderGraphMetadata& metadata);

// The ONE thin, file-writing wrapper - PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #13: always writes to a fixed, working-directory-relative
// path, "render_graph_export.dot" (overwritten every call, never a save
// dialog/path picker - this repo has none). Returns the resolved path
// actually written (useful for the caller to GTE_LOG_INFO) - std::string
// path on success; throws/returns an empty string on a genuine write
// failure (confirm this repo's own existing convention for "a file write
// failed" - e.g. Editor/SceneIO.h's own SaveScene() - and mirror it exactly
// rather than inventing a new error-reporting shape here).
std::string ExportRenderGraphDotToFile(const rg::RenderGraphMetadata& metadata);

} // namespace gte
```

Note the namespace: `namespace gte` (NOT `gte::rg`), taking a `rg::`-qualified
parameter — this mirrors `AtmosphereAerialPerspectiveSkyPurityValidation.h`'s
own exact shape (a `namespace gte` free function taking `const rg::RenderGraph&`)
rather than `RenderGraphMetadata.h`'s own `namespace gte::rg`, since this file
is an Editor-tier consumer of `gte::rg` types, not itself part of the
`gte::rg` module.

`BuildRenderGraphDot()`'s exact DOT syntax is an implementation detail this
phase owns end-to-end (there is no existing DOT-writing code anywhere in
this repo to mirror — `search_in_dir` for `digraph`/`.dot` under `src/`
confirms zero hits) — keep it simple and valid: escape any `"` inside a pass/
resource name before embedding it in a DOT string literal (Graphviz node/
edge labels are double-quoted strings), and give every node a stable,
sanitized `ID` (e.g. `pass_0`, `pass_1`, ... plus a distinct `resource_...`
prefix) SEPARATE from its human-readable `label="..."` attribute, since a
raw pass/resource name is not guaranteed to be a valid bare DOT identifier
(spaces, `#`, etc. are common in real pass/resource names in this engine).

### Step 3.5 — CMake wiring

Add to the root `CMakeLists.txt`'s `gte_editor` source list (the
`add_library(gte_editor STATIC ...)` block — NOT the `gte_core` block
`RenderGraphMetadata.cpp` lives in), placed alongside this campaign's other
Editor-tier debug-tool precedents (immediately after
`src/Editor/FrameDebuggerPreviewProcessing.cpp`, right before the
`src/Editor/Panels/HierarchyPanel.h` block starts — re-confirm the exact
current line/neighbor via `search_in_dir` for `FrameDebuggerPreviewProcessing.cpp`
before editing, since PHASE1/PHASE2's own new `gte_core` entries do not
shift this later `gte_editor` block, but another concurrently-landed
campaign might have):

```
src/Editor/RenderGraphDotExport.h
src/Editor/RenderGraphDotExport.cpp
```

Add a new Tier-1 test file, `tests/Editor/RenderGraphDotExportTests.cpp`
(assert `BuildRenderGraphDot()`'s output contains every pass name, is valid
enough to at least start with `"digraph"` and balance braces — a full
Graphviz-syntax-correctness check is out of scope, a sane-shape smoke
assertion is enough), registered in `tests/CMakeLists.txt`'s `Editor/` test
list alongside `Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp`
(confirmed current neighbor, `tests/CMakeLists.txt` — this is the correct
list: `GreatTamanaEngineTests` already links `gte_editor`, so a `gte_editor`-
tier pure function is fully Tier-1-testable here exactly like that file's
own tests already are).

### Verification

1. Incremental build: `cmake --build build`.
2. Run the new `RenderGraphDotExportTests.cpp` cases.
3. Live smoke test: `run_app_background`, `GET /activate_tab?name=Render%20Graph`,
   take a screenshot (`gte_send_request` against `GET /get_swapchain`,
   `load_image`) and manually confirm it is pixel-for-pixel unchanged from
   the PHASE1 baseline screenshot (same tables, same text, same columns,
   Pause/un-pause both still behave identically — test BOTH: pause, confirm
   the display freezes; un-pause, confirm it resumes updating).
4. Click (or otherwise trigger, via whatever this repo's own network/UI
   automation supports — confirm `POST`-triggerable UI actions exist for a
   plain `ImGui::Button` before assuming one does; if none does, this one
   click may need to be a genuinely manual verification step noted plainly
   in the completion report) the "Export DOT" button, confirm
   `render_graph_export.dot` was written (its expected working-directory
   path — likely the `build/` directory the exe runs from; confirm via
   `GET /get_logs` showing the exact `GTE_LOG_INFO` line this phase adds),
   and `read_file` it to confirm it is well-formed, non-empty DOT text
   naming real passes from this exact session.
5. `stop_app_background` afterward. `git_status` — confirm the diff touches
   exactly: `RenderGraphPanel.h/.cpp`, the two new
   `src/Editor/RenderGraphDotExport.h/.cpp` files, the new
   `tests/Editor/RenderGraphDotExportTests.cpp` file, and the two
   `CMakeLists.txt` files.

### What this phase does NOT do

- Does not touch `FrameCaptureBridge`/`NetworkRoutes.h`/`NetworkServer.cpp`/
  `EditorHost.cpp` — PHASE4.
- Does not add a file-save dialog for "Export DOT" (Locked Design
  Decision #13, unchanged).
- Does not change `RenderGraphPanel::Build()`'s own public signature.
- Does not place any new file under `src/Renderer/RenderGraph/` — PHASE2
  already added everything this campaign needs there; this phase's own new
  file lives under `src/Editor/` instead (Locked Design Decision #14).

### Completion

Write `PHASE3_COMPLETION_REPORT.md` (before/after screenshot comparison,
the exact DOT file content produced by a real session, pasted verbatim or
attached), then `git_add` + `git_commit`.
