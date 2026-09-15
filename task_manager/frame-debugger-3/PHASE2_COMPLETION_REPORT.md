# PHASE2 — The real `FrameDebuggerSnapshot` builder — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md` (already landed).

## Summary

Implemented PHASE2's own "Step 3: The Plan" — a new, pure, Tier-1-tested
`BuildRealFrameDebuggerSnapshot()` function added to the existing
`src/Editor/FrameDebuggerData.h`/`.cpp` (extending that file across phases,
matching `frame-debugger-2`'s own precedent), reshaping PHASE1's real
`FrameDebuggerCaptureContext`/`DescribeStandardPipelineState()` plus the
ALREADY-REAL `gte::rg::RenderGraphSnapshot` into a real, non-empty
`FrameDebuggerSnapshot` filtered to ONLY the Game View's own passes — with
**zero changes to `Panels/FrameDebuggerPanel.cpp`** (not touched at all this
phase, per that campaign's own explicit promise and this phase's own Step
3.4).

### What the new builder does

`FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(const rg::RenderGraphSnapshot& graphSnapshot, const FrameDebuggerCaptureContext& capture, const std::vector<std::string>& gpuSkinningPassNamesThisFrame, const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo = {})`:

1. Looks up the real `"GameView"` entry in `graphSnapshot.passesInExecutionOrder`
   by name. If absent, returns an honest, empty `FrameDebuggerSnapshot{}` —
   exactly the same "no frame captured yet" shape
   `BuildPlaceholderFrameDebuggerSnapshot()` already models.
2. Builds one root group node `"Game View"` (`isDrawCall = false`).
   - If `gpuSkinningPassNamesThisFrame` is non-empty **and at least one real
     pass in `graphSnapshot` actually matches one of those names**, adds a
     `"GPU Skinning"` child group containing one LEAF per matching real pass,
     in `graphSnapshot`'s own real execution order (never the unrelated order
     the name list happens to be in). Each leaf: `eventLabel = "Compute
     Dispatch"`, `shaderName` = the pass's own real name, `passName = "GPU
     Skinning"`, every blend/Z/stencil field `"n/a (compute pass)"`, one real
     `"GPU Time (ms)"` vector entry read from that pass's own
     `PassGpuStats::timing.milliseconds` (see "Deviations" below for why NOT
     a fabricated all-zero `DrawStats` row), `textures`/`matrices` left empty
     (real — a compute skinning pass never samples a material texture or
     uses a camera matrix).
   - Adds one final LEAF sibling (never nested inside the GPU-skinning group)
     for the real `"GameView"` pass itself: `eventLabel = "Draw Mesh"`,
     `passName = "GameView"`, `shaderName` = every DISTINCT real Pipeline
     debug name `capture` recorded this frame (comma-joined),
     `textures` = one row per DISTINCT real `MaterialTexture` debug name
     `capture` recorded (generic `"Material Texture"` label, since there is
     no per-slot naming convention in this engine yet), `vectors` = a real
     `"Clear Color"` entry plus a real `"Draw Stats (Calls, Tris)"` entry
     taken directly from `graphSnapshot`'s own `"GameView"` pass's
     `stats.drawStats` (NOT `capture.DrawCallCount()` — the graph snapshot's
     own stats are the authoritative source other panels, e.g. "Render
     Graph", already trust), `matrices` = one real `"ViewProjection"` entry
     built from `capture.LastViewProjection()`, and blend/Z/stencil rows from
     `DescribeStandardPipelineState()`'s real strings.
3. `eventIndex` is assigned sequentially, starting at 0, across the WHOLE
   tree (every GPU-skinning leaf first, in execution order, then the
   `"GameView"` leaf last) — `totalEventCount` is simply the count of leaf
   nodes actually produced (see "Deviations" below for a small wording
   clarification against Step 3.1 point 3's own text).
4. `renderTarget` is a real `FrameDebuggerRenderTargetInfo` — see
   "Deviations" below for exactly how `width`/`height`/`format` are handled.

### The row-major vs. column-major matrix transpose requirement (Step 3.1)

`Mat4` (`src/Math/Mat4.h`) is COLUMN-MAJOR storage (`columns[c][r]`,
`operator()(row, col)` returns `columns[col][row]`, `Data()` is a contiguous
column-major `float[16]`). `FrameDebuggerMatrixProperty::values` is
ROW-MAJOR (`values[row*4 + col]`). The implementation copies element-by-
element through `Mat4::operator()(row, col)` (`viewProjection.values[row*4 +
col] = matrix(row, col)`) — **never** a raw `memcpy`/direct copy of
`Mat4::Data()`, which would silently produce a transposed matrix. A dedicated
regression test (`ViewProjectionMatrixRoundTripsWithoutTransposing`, see
below) uses a deliberately non-symmetric, hand-picked matrix specifically
chosen so a transpose bug would produce visibly wrong values at every
off-diagonal index, not a by-coincidence-correct result.

## Deviations from the phase document

1. **`gameViewRenderTargetInfo` was added as a new, defaulted 4th parameter**,
   not present in the phase document's own illustrative Step 3.1 code
   snippet. Step 3.1 point 4 explicitly requires real `width`/`height`/
   `format` values (from the Game View `RenderTexture`'s own current extent
   and `Renderer::ColorFormat()`), but the function itself is required to
   stay pure ("no live VkDevice/Renderer, exactly like
   `BuildRenderGraphSnapshot()` itself" — Step 2). Reaching into a live
   `RenderTexture`/`Renderer` from inside this function would violate that
   purity requirement outright, and the 3-parameter signature the document
   shows has no way to obtain that data at all. The resolution mirrors the
   document's OWN existing precedent one parameter to its left
   (`gpuSkinningPassNamesThisFrame` is likewise a plain, already-resolved
   value the CALLER computes from a live source, not something this function
   reaches out for itself): a plain `FrameDebuggerRenderTargetInfo` parameter,
   defaulted to `{}` so every Tier-1 test in this phase can omit it, with only
   its `width`/`height`/`format` fields actually used (its `name` field is
   always overwritten to `"GameView"` for a non-empty result — the real
   render-target name is already known statically, it is not something that
   needs to come from a live object). A future phase (PHASE3/PHASE4, wherever
   the panel is finally wired to a live `Renderer`/`RenderTexture`) supplies
   the real resolved values; this phase's own tests supply hand-fabricated
   ones (see `RenderTargetInfoIsRealAndNamedGameView`).
2. **The real clear-color value (`kGameClearColor` from
   `RenderPasses.cpp`) is duplicated as a new, local constant
   (`kFrameDebuggerGameClearColor`) inside `FrameDebuggerData.cpp`, rather
   than referenced directly from `RenderPasses.cpp`/`.h`.** The phase
   document's own wording ("reference that exact constant, do not re-guess
   it") assumes the constant is reachable from this file, but investigation
   found it declared inside an anonymous namespace in `RenderPasses.cpp`
   (internal linkage — not visible outside that one translation unit), and
   promoting it to a shared header would require `src/Editor/` to
   `#include` an `src/Application/` header. Per `AGENTS.md`'s "Clean
   Architecture" rule, `Application` is the composition root that already
   depends on `Editor` (e.g. `ImGuiEditorLayer`) — the reverse direction
   would be a genuine architectural inversion, not a harmless convenience
   include, and no other `src/Editor/` file does this anywhere in the
   codebase today (confirmed by search). Instead, this deviation follows a
   precedent `RenderPasses.cpp` ITSELF already established for this exact
   same value: that file's own `kGameClearColor` is already a hand-
   duplicated copy of `Game::Render()`'s hardcoded `renderer.Clear(20, 20,
   30, 255)` call, with a code comment documenting the duplication and
   instructing future maintainers to keep the two in sync. This phase adds
   a second link in that same, already-accepted chain, with an equally
   explicit comment. The numeric value is bit-for-bit identical
   (`20/255, 20/255, 30/255, 1.0f`).
3. **Step 3.1 point 3's own wording ("2 if there is no GPU skinning this
   frame — just the `"GameView"` leaf — plus 1 per GPU-skinning leaf")
   appears to be a documentation typo**, since it directly contradicts that
   same phase document's own Step 3.3 test specification ("`"GameView"`
   present with zero GPU-skinning passes -> **exactly one leaf**, no `"GPU
   Skinning"` group at all"). The implementation follows Step 3.3's
   unambiguous, more specific wording: `totalEventCount` is simply the total
   count of leaf nodes actually produced (1 with no GPU skinning this frame,
   3 with two GPU-skinning passes plus the `"GameView"` leaf — matching the
   Tier-1 tests below exactly). No further action needed; flagging this here
   per this task's own "clearly document any such deviation" instruction.
4. **A GPU-skinning leaf's `vectors` reports real GPU timing
   (`"GPU Time (ms)"`, from `PassGpuStats::timing.milliseconds`) instead of
   a `DrawStats`-shaped row.** Step 3.1 point 2 itself already anticipates
   this ambiguity ("confirm exactly what `PassGpuStats` reports for a
   compute pass at implementation time — likely just GPU timing, no
   `DrawStats`"): `Renderer::Dispatch()`'s own doc comment (confirmed by
   reading `RenderPasses.cpp`/`DrawStats.h`) explicitly states it never
   touches `PassGpuStats::drawStats`, so that field is always the default
   `{0, 0}` for a compute pass — reporting it as a "Draw Stats" row would
   misleadingly imply a draw call happened when none did. Reporting the
   pass's own real GPU timing sample instead is a strictly more honest
   choice, and is still "the real `DrawStats`-adjacent numbers available for
   a compute pass today" the phase document itself asks for.
5. Everything else (empty-result rule, tree shape, Game-View-only filter,
   pass-scoped aggregated shader/texture/vector/matrix "reflection", blend/
   Z/stencil rows from `DescribeStandardPipelineState()`) matches the phase
   document's Step 3.1 exactly, with no further deviation.

## New/modified files

- `src/Editor/FrameDebuggerData.h` — added `#include "FrameDebuggerCapture.h"`
  and `#include "../Renderer/RenderGraph/RenderGraphSnapshot.h"` (both safe:
  this file only ever compiles under `GTE_ENABLE_EDITOR`, exactly like
  `FrameDebuggerCapture.h` itself), plus the new
  `BuildRealFrameDebuggerSnapshot()` declaration (fully documented, see
  above). No existing struct/function signature changed.
- `src/Editor/FrameDebuggerData.cpp` — the new builder's real implementation
  (`BuildRealFrameDebuggerSnapshot()`, plus three small anonymous-namespace
  helpers: `FindPassByName()`, `Contains()`, `BuildGpuSkinningLeaf()`,
  `BuildGameViewLeaf()`, and the new `kFrameDebuggerGameClearColor` constant
  — see Deviation #2 above).
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (**new file**) — 7
  Tier-1 tests covering every case from this phase's own Step 3.3, plus two
  extra cases surfaced during implementation:
  - `NoGameViewPassProducesEmptyResult`
  - `GameViewWithNoGpuSkinningProducesExactlyOneLeaf` (also proves
    `"SceneView"`/`"Present"` passes present in the SAME underlying
    `graphSnapshot` are correctly excluded — Locked Design Decision #7)
  - `TwoGpuSkinningPassesProduceGroupPlusGameViewLeaf` (3 total events,
    correct sequential `eventIndex` values across the whole tree)
  - `GpuSkinningNameWithNoMatchingRealPassAddsNoGroup` (the Step 2 fallback
    case: a caller-supplied GPU-skinning pass name with no matching real
    pass this frame must never produce an empty, misleading group)
  - `DistinctPipelineAndTextureNamesProduceDistinctEntriesNotDuplicates`
  - `ViewProjectionMatrixRoundTripsWithoutTransposing` (the row/column-major
    regression test described above)
  - `RenderTargetInfoIsRealAndNamedGameView` (Deviation #1's own new
    parameter)
- `tests/CMakeLists.txt` — registered the new test file right after
  `Editor/FrameDebuggerCaptureTests.cpp`, inside the existing
  `if(GTE_ENABLE_EDITOR)` block.

No other files were touched — in particular, `Panels/FrameDebuggerPanel.cpp`
is completely untouched (per Step 3.4, wiring the builder into the panel is
PHASE3/PHASE4's job), and no Scene View/Present pass is ever added to the
tree under any circumstance.

## Compile check (fast, per this phase's own Step 3.5 — not a full rebuild/
regression)

1. Built `gte_core` in the existing `build` directory
   (`GTE_ENABLE_EDITOR=ON`, the configured default):
   ```
   cmake --build build --target gte_core
   ```
   Result: **succeeded**, no warnings/errors from any new or modified file.

2. Built `GreatTamanaEngineTests` and ran it filtered to every Frame-
   Debugger-related test:
   ```
   cmake --build build --target GreatTamanaEngineTests
   build\tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebugger*
   ```
   Result: **all 19 tests passed** — the 7 new
   `FrameDebuggerSnapshotBuilderTest.*` cases, plus the pre-existing 6
   `FrameDebuggerCaptureContextTest.*`/1 `DescribeStandardPipelineStateTest.*`
   (PHASE1, unaffected) and 6 `FrameDebuggerDataTest.*` (`frame-debugger-2`,
   unaffected).

No full clean build and no full `ctest` regression suite were run in this
phase — per both this phase's own Step 3.5 and PHASE0's Step 3.6/"Order of
work", that is reserved for the final PHASE8 step only.

## Next step

PHASE3 (`PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md`) — a real
multi-frame ring buffer (`FrameDebuggerHistory`), each slot holding one real
snapshot (built by this phase's `BuildRealFrameDebuggerSnapshot()`) plus a
retained GPU copy of that historical frame's real Game View output texture;
wires the actual capture TRIGGER (Enable edge / Step / an explicit Capture
button). PHASE3 is also where a live caller finally has access to the real
Game View `RenderTexture`/`Renderer` to resolve this phase's new
`gameViewRenderTargetInfo` parameter with genuine live `width`/`height`/
`format` values (see Deviation #1 above).
