# PHASE3 COMPLETION REPORT — Per-Draw-Call Entity Attribution Capture Infrastructure

Campaign: `frame-debugger-6`. Phase: PHASE3 (Workstream B foundation). Branch:
`feature/frame-debugger-impl` (unchanged, per prerequisites — no branch switch
performed).

## Summary

Implemented `PHASE3_PER_DRAW_ENTITY_ATTRIBUTION_CAPTURE_INFRASTRUCTURE.md`
exactly as specified, Steps 1–5, with no deviation. `DrawCommand`
(`src/Game/RenderSystem.h`) now carries the real ECS `Entity` it came from,
populated by `RenderSystem::CollectRenderables()`.
`FrameDebuggerCaptureContext` (`src/Editor/FrameDebuggerCapture.h/.cpp`) gained
a new, never-deduplicated `FrameDebuggerDrawRecord` list
(`RecordEntityDraw()`/`DrawRecords()`), populated alongside the existing
`RecordDraw()` call inside `RenderSystem::Draw()`'s already-existing
`#if GTE_ENABLE_EDITOR` / `if (capture != nullptr)` block, using Option (a)
from the strategy document (two separate calls, `RecordDraw()` left
completely untouched). Each record carries the entity's index/generation, its
resolved display name (`Name` component value, or the exact same synthesized
`"Entity <index>"` fallback format `HierarchyPanel::BuildEntityLabel()` already
uses for a non-Camera entity), the real Pipeline/MaterialTexture debug names,
and a real per-draw triangle count computed with the exact same
`HasIndexBuffer() ? IndexCount()/3 : VertexCount()/3` formula
`DrawStats.h::AccumulateDrawStats()` already uses. This phase changes **zero**
Frame Debugger tree/display behavior — verified live below, the tree is
byte-for-byte identical to PHASE2's end state (`totalEventCount:7`, same 5+1+1
leaf shape) — the new data exists but is not yet consumed anywhere; that is
PHASE4's job.

## Files Changed

### Core capture infrastructure
- `src/Game/RenderSystem.h` — `DrawCommand` gained a new first field,
  `Entity entity;`, with a doc comment referencing this campaign/phase and
  `FrameDebuggerCaptureContext::RecordEntityDraw()`. `RenderSystem.h` already
  transitively includes `Entity.h` via `ECS/Registry.h` — confirmed, no new
  `#include` needed.
- `src/Game/RenderSystem.cpp`:
  - `CollectRenderables()` — the `DrawCommand{...}` aggregate-init call now
    passes `entity` as its new first argument (the loop already had `entity`
    as a local variable — no new lookup needed).
  - Added `#include "ECS/Components/Name.h"` (a plain, always-compiled, core
    ECS component — safe per `AGENTS.md`'s Clean Architecture rule, no
    layering violation, confirmed by reading `Name.h` itself).
  - `Draw()`'s existing `#if GTE_ENABLE_EDITOR` / `if (capture != nullptr)`
    block, immediately after the existing `capture->RecordDraw(...)` call,
    now additionally: computes `triangleCount` via
    `mesh->HasIndexBuffer() ? (mesh->IndexCount() / 3) : (mesh->VertexCount() / 3)`;
    resolves `displayName` from `registry.TryGetComponent<Name>(command.entity)`
    (falling back to `"Entity " + std::to_string(command.entity.index)` when
    absent/empty — re-verified against `HierarchyPanel::BuildEntityLabel()`'s
    real source, whose non-Camera branch synthesizes the identical `"Entity %u"`
    text via `snprintf`; the Camera-only `" (Camera)"` suffix branch does not
    apply here since draws only ever come from `MeshRenderer` entities); and
    calls `capture->RecordEntityDraw(command.entity.index,
    command.entity.generation, displayName, pipeline->DebugName(),
    materialTextureDebugName, triangleCount);`.
- `src/Editor/FrameDebuggerCapture.h`:
  - Added `#include <cstdint>` (for the new `std::uint32_t` fields).
  - Added `struct FrameDebuggerDrawRecord` (entityIndex/entityGeneration/
    displayName/pipelineDebugName/materialTextureDebugName/triangleCount),
    positioned right after `FrameDebuggerStandardPipelineState`, exactly as
    the strategy document specifies.
  - Added `FrameDebuggerCaptureContext::RecordEntityDraw(...)` (declaration)
    immediately after `RecordDraw()`, and `DrawRecords()` (an accessor
    returning `const std::vector<FrameDebuggerDrawRecord>&`) immediately after
    `LastViewProjection()`.
  - Added the new private member `std::vector<FrameDebuggerDrawRecord>
    m_drawRecords;`.
- `src/Editor/FrameDebuggerCapture.cpp`:
  - Added `#include <utility>` (for `std::move`).
  - Implemented `RecordEntityDraw()` exactly as the strategy document's own
    Step 3.4 code sample specifies — builds one `FrameDebuggerDrawRecord` and
    `push_back`s it, unconditionally (never deduplicated).
  - `Reset()` now also calls `m_drawRecords.clear();`.

### Design choice made (Step 3.3's explicit decision point)
Chose **Option (a)** — two separate calls at the `RenderSystem::Draw()` call
site (`RecordDraw(...)` unchanged, plus a new, separate
`RecordEntityDraw(...)` call) — exactly the strategy document's own
recommendation. `RecordDraw()`'s signature, behavior, and every existing test
for it are completely untouched; `RecordEntityDraw()` is purely additive. This
kept the diff small, surgical, and zero-risk to already-tested behavior, and
the two calls did not turn out to be awkwardly redundant in practice (they sit
right next to each other, sharing `pipeline->DebugName()`/
`materialTextureDebugName`, which is only computed once and passed to both).

### Tests (Step 4 — Testability & Regression Safety)
- `tests/Game/RenderSystemTests.cpp`:
  - `EntityWithMeshRendererAndTransformUsesWorldMatrix` — added
    `EXPECT_EQ(commands[0].entity, entity);`.
  - `MultipleEntitiesEachProduceTheirOwnDrawCommand` — now tracks the three
    created entities in a `std::vector<Entity> createdEntities` and asserts
    `commands[i].entity == createdEntities[i]` for every index, in addition to
    the existing mesh/pipeline handle checks.
- `tests/Editor/FrameDebuggerCaptureTests.cpp`:
  - `StartsEmpty` — added `EXPECT_TRUE(capture.DrawRecords().empty());`.
  - `ResetClearsEverythingBackToEmpty` — now also calls `RecordEntityDraw()`
    before `Reset()` and asserts `DrawRecords()` is empty afterward.
  - Three new cases: `RecordEntityDrawAppendsOneRecordPerCall` (two distinct
    calls produce two records with every field correctly populated, including
    an untextured second draw's empty `materialTextureDebugName`),
    `RecordEntityDrawNeverDeduplicatesRepeatedEntityMeshCombinations` (the
    exact same entity/mesh combination recorded twice still produces two
    separate records — the opposite of `RecordDraw()`'s own dedup behavior),
    and `RecordEntityDrawDoesNotAffectRecordDrawBookkeeping` (confirms
    `RecordEntityDraw()` never touches `PipelineDebugNames()`/
    `MaterialTextureDebugNames()`/`DrawCallCount()`, proving Option (a)'s
    additive-only guarantee).

## Deviations From The Strategy Document

None. Every instruction in Steps 1–5 was followed as written. The one
explicit decision point the document itself calls out (Step 3.3, "(a) vs
(b)") was resolved in favor of the document's own recommended Option (a), as
instructed, after confirming (per the document's own guidance) that it was
not awkwardly redundant in practice.

## Evidence

### Incremental build
`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed successfully —
`gte_core`, `GreatTamanaEngine.exe`, and `GreatTamanaEngineTests.exe` all
built and linked cleanly with `GTE_ENABLE_EDITOR=ON` (the default dev
configuration already present in `build/`). No compile errors or warnings
related to this phase's changes.

### Tests
Ran `build/tests/GreatTamanaEngineTests.exe` directly (not a full `ctest`
run, per this campaign's own "no full build/ctest except PHASE5" rule) with
`--gtest_filter=*RenderSystem*:*FrameDebugger*` — **97 tests, 97 passed, 0
failed**, including:
- The 2 updated `RenderSystemTest.*` cases (now asserting `DrawCommand::entity`)
  — pass.
- The 3 new `FrameDebuggerCaptureContextTest.RecordEntityDraw*` cases — all
  pass.
- The 2 updated `FrameDebuggerCaptureContextTest.StartsEmpty`/
  `ResetClearsEverythingBackToEmpty` cases (now also covering `DrawRecords()`)
  — pass.
- Every pre-existing `RenderSystemTest`/`FrameDebuggerCaptureContextTest`/
  `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerDataTest`/
  `FrameDebuggerHistoryTest`/`FrameDebuggerCommandBridgeTest`/
  `ParseFrameDebugger*QueryTests`/`BuildFrameDebugger*ResponseJsonTests` case
  — still passes unchanged (including PHASE1/PHASE2's own ViewScope cases),
  confirming no regression.

### Manual/Live Verification (Step 5's own Definition of Done)
1. Launched `build/GreatTamanaEngine.exe` via `run_app_background`.
2. `POST /load_scene` with body `{}` →
   `{"resolved_path":"...\\Project\\TestScene.gtscene","success":true}`.
3. `GET /frame_debugger/open` → `windowOpen:true`.
4. `GET /frame_debugger/enable?value=true` → `enabled:true, historyCount:1,
   totalEventCount:7` — **identical to PHASE2's own end-state number**,
   confirming this phase added zero new visible tree leaves.
5. `GET /get_swapchain` — screenshot confirms the Frame Debugger window still
   shows exactly the PHASE2 end-state tree shape:
   - `"Compute Dispatches (Pre-GameView)"`: `AtmosphereTransmittanceLutPass`,
     `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`,
     `AtmosphereAerialPerspectiveVolumePass`,
     `AtmosphereAerialPerspectiveVolumeDebugSlicePass` — 5 leaves, no per-entity
     children under `"GameView"` yet (expected — PHASE4's job).
   - `"GameView"` leaf itself (no expandable per-entity children yet).
   - `"Compute Dispatches (Post-GameView)"`: `AtmosphereAerialPerspectiveCompositePass`
     — 1 leaf.
   - Total: 5 + 1 + 1 = 7, matching `totalEventCount:7` exactly, byte-for-byte
     identical to PHASE2's own reported tree shape.
6. Stopped the background process via `stop_app_background`.

This confirms the new per-draw capture data (`FrameDebuggerDrawRecord`/
`DrawRecords()`) is being populated internally every armed frame (proven by
the Tier-1 tests above) but is genuinely NOT YET read by any tree-building
code, exactly as this phase's Definition of Done requires.

## Definition of Done — Checklist

- [x] `DrawCommand::entity` added and populated.
- [x] `FrameDebuggerDrawRecord` + `FrameDebuggerCaptureContext::
      RecordEntityDraw()`/`DrawRecords()` added; `Reset()` clears the new list.
- [x] `RenderSystem::Draw()` resolves a real display name (`Name` component or
      a verified-matching fallback) and a real per-draw triangle count, and
      calls `RecordEntityDraw()`.
- [x] `#if GTE_ENABLE_EDITOR` boundaries preserved exactly (the new
      `FrameDebuggerDrawRecord`/`RecordEntityDraw()`/`DrawRecords()`
      dereferences all live inside the same pre-existing `#if GTE_ENABLE_EDITOR`
      block `RecordDraw()` already lived in; `RenderSystem.h`'s own forward
      declaration and `#include "ECS/Components/Name.h"` are both safe for a
      `GTE_ENABLE_EDITOR=OFF` build to compile+link, since `Name.h` is a core,
      always-compiled ECS component with no Editor dependency).
- [x] New/updated Tier-1 tests pass (97/97, gtest_filter above).
- [x] Incremental build succeeds.
- [x] Manual sanity check: the Frame Debugger UI is unchanged from PHASE2's
      end state — same `totalEventCount:7`, same 5+1+1 tree shape, confirmed
      via live HTTP + screenshot.
- [x] Commit via `git_add`/`git_commit`.

## Next Phase

PHASE4 (`PHASE4_GAMEVIEW_PER_ENTITY_DRAW_TREE_LEAVES.md`) is the actual
user-facing feature — it consumes this phase's new `FrameDebuggerCaptureContext::
DrawRecords()` data inside `FrameDebuggerData.cpp`'s snapshot builder to give
the `"GameView"` tree node real, selectable per-entity child leaves (e.g.
`"terrain (Entity 2)"`), plus the required `RenderEventNode()` fix in
`Editor/Panels/FrameDebuggerPanel.h/.cpp` (Step 3.3).
