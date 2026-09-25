# PHASE2 — `RenderGraphMetadata` Data Model + `BuildRenderGraphMetadata()` + JSON — COMPLETION REPORT

**Status: DONE.** No deviation from the phase plan
(`PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md`) or the parent
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions #1/#2/#5/#6/#10/#11 —
this phase is purely additive: two new files under `src/Renderer/RenderGraph/`
(`gte_core`-tier), one new Tier-1 test file, and two `CMakeLists.txt` wiring
edits. Nothing under `src/Editor/`, `src/Application/`, or `src/Network/` was
touched.

## What was done

1. **New file pair**: `src/Renderer/RenderGraph/RenderGraphMetadata.h` + `.cpp`
   (`namespace gte::rg`). Re-verified every field name/type against the real,
   current headers before writing any struct (`RenderGraphSnapshot.h`,
   `RenderGraphTypes.h`, `RenderPassGroupRegistry.h`,
   `GpuDrivenBatchDebugInfo.h`, `RenderFeatureDebugEntry.h`, `RenderGraph.h`
   for the exact `ExecuteTimingMode` enumerator names, `GpuTiming.h` for
   `GpuTimingSample`'s exact field names) — every fact the phase file
   paraphrased matched the real source exactly, so no correction was needed
   anywhere.
   - `RenderGraphResourceRefMetadata { name, kind }`.
   - `RenderGraphPassMetadata { name, isCulled, kind, category, drawKind,
     viewScope, renderPassEvent, tagGroupLabel (optional), reads, writes,
     drawCallCount, triangleCount, gpuTimingText, gpuTimingMilliseconds
     (optional) }`.
   - `RenderGraphResourceMetadata { name, isImported, firstUsePassIndex,
     lastUsePassIndex, firstUsePassName (optional), lastUsePassName
     (optional) }`.
   - `RenderGraphRegimeMetadata { regimeName, passes, resources,
     timingSlotBudgetExhausted }`.
   - `RenderGraphMetadata { schemaVersion = 1, offscreenRegime, presentRegime,
     gpuDrivenBatches, renderFeatures }` — offscreen/present are two NAMED
     fields (Locked Design Decision #1), all three of the panel's data
     sources folded in (Locked Design Decision #2).
   - `BuildRenderGraphMetadata(offscreen, present, gpuDrivenBatches,
     renderFeatures)` — pure, takes only already-computed
     `RenderGraphSnapshot`/`GpuDrivenBatchDebugInfo`/`RenderFeatureDebugEntry`
     values, no live `RenderGraph&`/`VkDevice`/`Renderer`/`Core` anywhere.
   - `to_json(nlohmann::json&, const RenderGraphMetadata&)` declared in the
     header (the one externally-consumed entry point); every nested
     `to_json` (`RenderGraphResourceRefMetadata`/`RenderGraphPassMetadata`/
     `RenderGraphResourceMetadata`/`RenderGraphRegimeMetadata`) is defined
     TU-locally inside `RenderGraphMetadata.cpp`, in dependency order (each
     sub-struct's own `to_json` appears before the first `to_json` that
     embeds it), so ordinary unqualified lookup finds every one of them with
     no reliance on cross-TU ADL timing subtleties.
   - `to_json(nlohmann::json&, const GpuDrivenBatchDebugInfo&)` /
     `to_json(nlohmann::json&, const RenderFeatureDebugEntry&)` — declared in
     the header inside `namespace gte` (matching each struct's own
     namespace, Locked Design Decision #6), defined in
     `RenderGraphMetadata.cpp`. Neither `GpuDrivenBatchDebugInfo.h` nor
     `RenderFeatureDebugEntry.h` gained a `nlohmann/json.hpp` include — both
     stay exactly as dependency-free as their own doc comments promise.
   - `#include <nlohmann/json.hpp>` — confirmed this is the exact existing
     include-path/style via `src/ECS/Reflection/MathJsonAdapters.h`, copied
     verbatim.
   - JSON keys are `snake_case` throughout (`schema_version`,
     `offscreen_regime`, `regime_name`, `is_culled`, `draw_kind`,
     `view_scope`, `render_pass_event`, `tag_group_label`, `draw_call_count`,
     `triangle_count`, `gpu_timing_text`, `gpu_timing_milliseconds`,
     `is_imported`, `first_use_pass_index`, `last_use_pass_index`,
     `first_use_pass_name`, `last_use_pass_name`,
     `timing_slot_budget_exhausted`, `gpu_driven_batches`, `batch_name`,
     `instance_count`, `visible_count`, `render_features`, `blend_mode`) —
     matching every existing `NetworkRoutes.cpp` JSON body's own convention.
     `std::nullopt` always serializes to a real JSON `null`, never an
     empty-string placeholder.

2. **New Tier-1 test file**:
   `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` — 11 new tests,
   mirroring `RenderGraphSnapshotTests.cpp`'s own include/namespace/`TEST()`
   registration style, but hand-fabricating `RenderGraphSnapshot`/
   `RenderGraphPassSnapshot`/`RenderGraphResourceSnapshot` values DIRECTLY (no
   `RenderGraphBuilder`/`RenderGraphCompiler::Compile()` anywhere in this
   file — `BuildRenderGraphMetadata()`'s own real input type is already
   `RenderGraphSnapshot`, so no compiled graph is needed to exercise it).
   Covers every case the phase plan's "Minimum cases" list asked for:
   - Empty snapshots (both regimes) + empty batches/features →
     `schemaVersion == 1`, everything else empty/false.
   - A surviving pass whose `tags` match a REGISTERED
     `RenderPassGroupRegistry` label → `tagGroupLabel` resolves correctly
     (`RegisterPassGroupLabel()`/`ResetPassGroupRegistryForTesting()` used
     exactly like `RenderPassGroupRegistryTests.cpp`'s own precedent).
   - A pass with `tags == 0` (no tags at all) → `tagGroupLabel` is
     `std::nullopt`.
   - A pass whose `tags` carry a real, but UNREGISTERED, bit →
     `tagGroupLabel` is still `std::nullopt`.
   - A CULLED pass (with a non-default `kind`/`category`/`drawKind`/
     `viewScope`/`renderPassEvent` and real reads/writes) → `drawCallCount`/
     `triangleCount` are `0`, `gpuTimingText == "N/A"`,
     `gpuTimingMilliseconds` is `std::nullopt`, but every other field is
     still correctly populated.
   - A resource with `firstUsePassIndex/lastUsePassIndex >= 0` → both
     `firstUsePassName`/`lastUsePassName` resolve to the correct pass names.
   - A resource with `firstUsePassIndex == -1` → both names are
     `std::nullopt`.
   - A `GpuTimingSample` with `Status::Present` and `milliseconds = 3.14` →
     `gpuTimingText == "3.14 ms"` AND `gpuTimingMilliseconds` holds `3.14`.
   - `timingSlotBudgetExhausted` copies through independently per regime.
   - `gpuDrivenBatches`/`renderFeatures` are copied through unchanged,
     field-by-field.
   - A full `to_json(RenderGraphMetadata)` round-trip test, asserting real
     keys/values/`null`s in the resulting `nlohmann::json` — this is the test
     that actually proves the JSON contract (see below for its own literal
     output).

3. **CMake wiring** — re-confirmed exact current line numbers via
   `search_in_dir` before editing (both had shifted since PHASE0's own
   estimate, due to PHASE1's new file-pair entries):
   - Root `CMakeLists.txt`: `RenderPassGroupRegistry.cpp` was at line 675
     (not PHASE0's estimated 673 — PHASE1's own two new `gte_core` entries
     shifted it by +2). Added `RenderGraphMetadata.h`/`.cpp` immediately
     after it (now lines 676-677).
   - `tests/CMakeLists.txt`: `RenderGraphSnapshotFormattingTests.cpp` was at
     line 2185 (PHASE1's own new entry, exactly where its own completion
     report said it added it). Added `RenderGraphMetadataTests.cpp`
     immediately after it (now line 2186).

## Verification evidence

1. **Incremental build** (`cmake --build build`): succeeded cleanly. Only
   the two new objects compiled/relinked
   (`RenderGraphMetadata.cpp.obj`, `RenderGraphMetadataTests.cpp.obj`), plus
   the expected re-link of `libgte_core.a`, `GreatTamanaEditor.exe`, and
   `tests\GreatTamanaEngineTests.exe`. The only warnings emitted are the
   pre-existing, unrelated MinGW static-CRT/plugin-linkage warnings this
   repo's build has always printed (identical text to PHASE1's own
   documented baseline) — zero new warnings from either new file.

2. **New tests run in isolation** (per Workflow Rule 1 — no full `ctest`):
   `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraphMetadataTest.*`
   → **11/11 PASSED**, 0 failed.

3. **Confirmed genuinely inert** (per this phase's own "Verification" step
   3): `search_in_dir` for `BuildRenderGraphMetadata(` across the whole repo
   returns hits ONLY inside `RenderGraphMetadata.h`/`.cpp`,
   `RenderGraphMetadataTests.cpp`, and the `task_manager/editor-core-
   separation-7/` strategy documents (plain-text planning docs, not compiled
   code) — **zero hits in any production `.cpp`/`.h` file**
   (`RenderGraphPanel.cpp`, `EditorHost.cpp`, `FrameCaptureBridge.*`,
   `NetworkRoutes.*`, `NetworkServer.cpp`, etc.). Nothing calls this new code
   from the running Editor yet, exactly as scoped — no live-engine HTTP smoke
   test was needed or performed for this phase.

4. **`git_status` re-confirmed immediately before this report/commit**: the
   working tree shows exactly the files this phase's plan says it may touch:
   `CMakeLists.txt` (modified), `tests/CMakeLists.txt` (modified), plus 3 new
   untracked files (`src/Renderer/RenderGraph/RenderGraphMetadata.h`,
   `src/Renderer/RenderGraph/RenderGraphMetadata.cpp`,
   `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp`). No other file
   was touched.

## Example `RenderGraphMetadata::to_json()` output (verbatim)

For the small, hand-fabricated input used by
`RenderGraphMetadataTest.ToJsonProducesExpectedTopLevelShapeAndNullHandling`
(one surviving `"RenderOpaque"` pass with default `kind`/`category`/
`drawKind`/`viewScope`/`renderPassEvent`, one read `"Depth"` (Texture), one
write `"Color"` (Texture), `drawCallCount = 2`, `triangleCount = 20`, default/
`Absent` GPU timing, `tags == 0` so no registered label; an empty present
regime; one GPU-driven batch `"Batch0"` with `instanceCount = 4` and no
`visibleCount` yet; one render feature `"Vignette"`), `nlohmann::json j =
metadata;` followed by `j.dump(2)` produces exactly:

```json
{
  "gpu_driven_batches": [
    {
      "batch_name": "Batch0",
      "instance_count": 4,
      "visible_count": null
    }
  ],
  "offscreen_regime": {
    "passes": [
      {
        "category": "General",
        "draw_call_count": 2,
        "draw_kind": "DrawMesh",
        "gpu_timing_milliseconds": null,
        "gpu_timing_text": "N/A",
        "is_culled": false,
        "kind": "Graphics",
        "name": "RenderOpaque",
        "reads": [
          {
            "kind": "Texture",
            "name": "Depth"
          }
        ],
        "render_pass_event": "Opaques",
        "tag_group_label": null,
        "triangle_count": 20,
        "view_scope": "Shared",
        "writes": [
          {
            "kind": "Texture",
            "name": "Color"
          }
        ]
      }
    ],
    "regime_name": "SynchronousImmediateReadback",
    "resources": [],
    "timing_slot_budget_exhausted": false
  },
  "present_regime": {
    "passes": [],
    "regime_name": "PipelinedDeferredReadback",
    "resources": [],
    "timing_slot_budget_exhausted": false
  },
  "render_features": [
    {
      "blend_mode": "Replace",
      "name": "Vignette",
      "priority": 1,
      "stage": "PreUI"
    }
  ],
  "schema_version": 1
}
```

(Object key order above reflects `nlohmann::json`'s own default
alphabetical-by-insertion `dump()` behavior for an `ordered_json`-less plain
`json` object — the REAL wire order a caller sees is whatever
`nlohmann::json`'s underlying map type produces; every key/value pair itself
is exactly what this phase's `to_json()` implementation emits, verified
field-by-field by the test above, which is the actual source of truth this
example is transcribed from.)

## What this phase deliberately did NOT do (unchanged from the plan)

- Did not change `RenderGraphPanel.cpp` at all — PHASE3.
- Did not add "Export DOT"/any Graphviz code — PHASE3.
- Did not touch `FrameCaptureBridge`/`NetworkRoutes.h`/`NetworkServer.cpp`/
  `EditorHost.cpp` — PHASE4.
- Did not run a full clean build or full `ctest` regression (reserved for
  PHASE5 per Workflow Rule 1).
- Did not run any live-engine HTTP smoke test — correctly not required per
  this phase's own "Verification" step 3 (confirmed genuinely inert, see
  above).
- Did not lock the FINAL, external JSON contract wording in stone forever —
  `schemaVersion` stays `1`; PHASE4/5's own live smoke test is the real,
  final proof this shape is genuinely useful to an external caller.

## Ambiguities encountered

None. Every fact needed (the real `RenderGraphSnapshot`/`RenderGraphTypes.h`/
`RenderPassGroupRegistry.h`/`GpuDrivenBatchDebugInfo.h`/
`RenderFeatureDebugEntry.h` field names/types, the exact current
`ExecuteTimingMode` enumerator names, the exact current CMake line numbers,
the existing `nlohmann::json` include-path/ADL-`to_json` convention) was
directly confirmed by reading the real source before writing any code,
exactly as the phase file instructed — no `ask_questions` call was needed.
