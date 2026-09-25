# PHASE4 — Cross-Thread Bridge + `GET /render_graph` HTTP Endpoint — COMPLETION REPORT

**Status: DONE.** No deviation from the phase plan
(`PHASE4_CROSS_THREAD_BRIDGE_AND_GET_RENDER_GRAPH_ENDPOINT.md`) or the parent
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions #3/#4/#7/#8/#9 — exactly
the files this phase's own plan said it may touch were touched, no more, no
less.

## What was done

1. **`src/Application/FrameCaptureBridge.h`**:
   - Rewrote the file-level header comment (it was already stale before this
     phase — it predated the texture-list `Publish*/Get*` pair and only ever
     described PNG bytes). It now honestly describes all THREE payload kinds
     this class moves across the thread boundary: captured PNG images, the
     published texture list, and now the published render-graph metadata.
   - Added `#include "../Renderer/RenderGraph/RenderGraphMetadata.h"`.
   - Added `PublishRenderGraphMetadata(rg::RenderGraphMetadata metadata)` /
     `GetPublishedRenderGraphMetadata() const`, mirroring
     `PublishTextureList()`/`GetPublishedTextureList()`'s exact doc-comment and
     locking shape.
   - Added `mutable std::mutex m_renderGraphMetadataMutex;` and
     `rg::RenderGraphMetadata m_publishedRenderGraphMetadata;` as brand-new
     private members, independent of `m_textureListMutex` and every `Slot`'s
     own mutex.

2. **`src/Application/FrameCaptureBridge.cpp`**: added the two new method
   bodies immediately after `GetPublishedTextureList()`, copying its exact
   `std::lock_guard`/`std::move`-on-publish/plain-copy-on-get shape verbatim,
   substituting the new mutex/field names.

3. **`src/Editor/EditorHost.cpp`**:
   - Added `#include "../Renderer/RenderGraph/RenderGraphMetadata.h"` (not
     previously transitively included).
   - Added the new publish call directly inside the existing
     `{ GTE_PROFILE_SCOPE("IEditorLayer::BuildUI"); ... }` block, immediately
     after the existing `m_editorLayer->BuildUI(...)` call, still inside the
     same braces — reusing the already-computed `renderFeatureEntries` local
     (Locked Design Decision #7 — `RenderFeatureCompositor::DebugSnapshot()`
     is still called at most once per `Run()` iteration) and calling
     `m_core.GetGpuDrivenBatchDebugInfo()` a second, harmless time (a cheap
     `const&` accessor with no "at most once" restriction, per its own doc
     comment):
     ```cpp
     m_captureBridge.PublishRenderGraphMetadata(rg::BuildRenderGraphMetadata(
         m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
         m_renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback),
         m_core.GetGpuDrivenBatchDebugInfo(), renderFeatureEntries));
     ```
   - Nothing else in `Run()` was touched — the existing `PublishTextureList()`
     block later in the same iteration is untouched.

4. **`src/Network/NetworkRoutes.h`/`.cpp`**:
   - `NetworkRoutes.h` gained
     `#include "../Renderer/RenderGraph/RenderGraphMetadata.h"` and a new
     declaration, `BuildRenderGraphMetadataResponseJson(const
     gte::rg::RenderGraphMetadata& metadata)`, with a doc comment explaining
     why this is the ONE place `rg::RenderGraphMetadata` crosses the
     Application/Network layer boundary AS-IS (its own `to_json()` is already
     the fully-resolved, plain-scalars-and-strings shape this route needs — no
     "resolve enums first" step is needed the way `TextureListEntryView`
     needed one).
   - `NetworkRoutes.cpp` implements it as a two-line pure function
     (`nlohmann::json body = metadata; return body.dump();`), placed
     immediately after `BuildListTexturesResponseJson()`.

5. **`src/Network/NetworkServer.cpp`**:
   - Added `RegisterRenderGraphRoute(httplib::Server&, FrameCaptureBridge*)`,
     mirroring `RegisterListTexturesRoute()` EXACTLY: null-check the bridge →
     `503` + `BuildGenericErrorResponseJson(...)` on null, otherwise fetch
     `GetPublishedRenderGraphMetadata()` → `BuildRenderGraphMetadataResponseJson()`
     → `200` + `application/json`.
   - Called `RegisterRenderGraphRoute(server, captureBridge);` immediately
     alongside the existing `RegisterListTexturesRoute(server, captureBridge);`
     call in `RegisterRoutes()`.
   - No query parameters (PHASE0_MASTER_STRATEGY.md's Locked Design Decision
     #8) — confirmed.

6. **`tests/Network/NetworkRoutesTests.cpp`**: added two new Tier-1 tests for
   `BuildRenderGraphMetadataResponseJson()`, hand-fabricating a small
   `gte::rg::RenderGraphMetadata` directly (no live `RenderGraph`/
   `BuildRenderGraphMetadata()` call in this file):
   - `DefaultConstructedMetadataProducesExpectedEmptyShape` — confirms
     `schema_version == 1`, both regime objects present with empty
     `passes`/`resources`/`""` `regime_name` (a bare, hand-fabricated
     `RenderGraphMetadata` never gets `BuildRenderGraphMetadata()`'s own
     regime-name population — that is PHASE2's own, already-covered
     responsibility, confirmed by `RenderGraphMetadataTests.cpp`), and empty
     `gpu_driven_batches`/`render_features`.
   - `PopulatedMetadataRoundTripsEveryTopLevelField` — one pass (with reads/
     writes), one GPU-driven batch, one render feature, asserting every field
     round-trips through `to_json()` → `dump()` → `nlohmann::json::parse()`
     correctly, including `tag_group_label` correctly serializing as JSON
     `null` and `visible_count` correctly serializing as JSON `null`.

   One test-writing mistake was made and fixed during this phase: the first
   draft of `DefaultConstructedMetadataProducesExpectedEmptyShape` incorrectly
   expected `"SynchronousImmediateReadback"`/`"PipelinedDeferredReadback"` for
   a BARE, hand-fabricated `RenderGraphMetadata` (that string is only ever
   populated by `BuildRenderGraphMetadata()` itself, which this test
   deliberately never calls) — caught immediately by running the new test in
   isolation, fixed to expect `""` instead, with an explanatory comment.

## No shape deviation from PHASE2's own sketch

The actual, real, shipped `GET /render_graph` JSON response body (captured
live below) matches PHASE2's own documented `to_json()` example byte-for-byte
in SHAPE (same top-level keys: `schema_version`, `offscreen_regime`,
`present_regime`, `gpu_driven_batches`, `render_features`; same nested pass/
resource keys) — nothing needed correcting for PHASE5's own docs pass.

## Verification evidence

### 1. Incremental build (`cmake --build build`)

Succeeded cleanly, twice (once after the initial implementation, once after
fixing the one test-expectation bug described above). Only the expected
objects recompiled/relinked: `FrameCaptureBridge.cpp.obj`,
`NetworkRoutes.cpp.obj`, `NetworkServer.cpp.obj`, `EditorHost.cpp.obj`,
`NetworkRoutesTests.cpp.obj` (plus a few pre-existing, untouched test object
files that share translation-unit dependencies — `FrameCaptureBridgeTests.cpp.obj`,
`NetworkRoutesImportAssetTests.cpp.obj`, `NetworkRoutesInstantiateAssetTests.cpp.obj`,
`CaptureEndpointsEndToEndTests.cpp.obj` — all recompiled cleanly with zero new
warnings), `libgte_core.a`, `libgte_editor.a`, `GreatTamanaEditor.exe`,
`GreatTamanaEngineTests.exe`. Only the same pre-existing, unrelated MinGW
static-CRT/plugin-linkage warnings this repo's build has always printed — zero
new warnings from any file this phase touched.

### 2. New tests run in isolation (per Workflow Rule 1 — no full `ctest`)

```
tests\GreatTamanaEngineTests.exe --gtest_filter=BuildRenderGraphMetadataResponseJsonTests.*:NetworkRoutesTests.*:BuildListTexturesResponseJsonTests.*
```

→ **9/9 PASSED**, 0 failed (2 new `BuildRenderGraphMetadataResponseJsonTests`
cases, plus the 3 pre-existing `NetworkRoutesTests` and 4 pre-existing
`BuildListTexturesResponseJsonTests` cases re-run as a regression sanity
check, confirmed byte-for-byte unaffected by this phase's changes).

### 3. Live, HTTP-driven, end-to-end smoke test — the real proof this campaign works

`run_app_background` on `build\GreatTamanaEditor.exe` (PID 12008):

**a) `GET /render_graph` — `200`, real current-frame JSON** (verbatim, first
call, truncated only where noted — full body confirmed well-formed JSON,
matching PHASE2's documented shape: `schema_version`, `offscreen_regime`,
`present_regime`, `gpu_driven_batches`, `render_features` all present):

```json
{"gpu_driven_batches":[],"offscreen_regime":{"passes":[{"category":"General","draw_call_count":0,"draw_kind":"DrawMesh","gpu_timing_milliseconds":0.20078124509811401,"gpu_timing_text":"0.20 ms","is_culled":false,"kind":"Compute","name":"AtmosphereTransmittanceLutPass","reads":[],"render_pass_event":"PreOpaques","tag_group_label":"Compute LUT","triangle_count":0,"view_scope":"Shared","writes":[{"kind":"Texture","name":"AtmosphereTransmittanceLut"}]},
 ... (AtmosphereMultiScatteringLutPass, AtmosphereSkyViewLutPass x2 (GameView+SceneView), AtmosphereAerialPerspectiveVolumePass x2, AtmosphereAerialPerspectiveVolumeDebugSlicePass) ...
 {"category":"General","draw_call_count":0,"draw_kind":"DrawMesh","gpu_timing_milliseconds":0.010833333068847656,"gpu_timing_text":"0.01 ms","is_culled":false,"kind":"Graphics","name":"RenderOpaque","reads":[],"render_pass_event":"Opaques","tag_group_label":null,"triangle_count":0,"view_scope":"GameView","writes":[{"kind":"Texture","name":"GameView"},{"kind":"Texture","name":"GameView"}]},
 ... (RenderOpaque SceneView, DrawSkyBackground x2, RenderTransparent x2, plugin render-feature passes, ...) ...
 ],"regime_name":"SynchronousImmediateReadback","resources":[...],"timing_slot_budget_exhausted":true},
 "present_regime":{"passes":[...],"regime_name":"PipelinedDeferredReadback","resources":[...],"timing_slot_budget_exhausted":false},
 "render_features":[{"blend_mode":"Replace","name":"DemoRenderFeatureV2","priority":0,"stage":"PostComposite"},{"blend_mode":"AlphaOver","name":"DemoRenderFeatureV2Second","priority":0,"stage":"PreUI"}],
 "schema_version":1}
```

(Full, real, un-truncated body was received and inspected as valid JSON;
truncated here purely for report readability — every top-level key named in
this phase's own plan is present and populated with real, current-frame data.)

**b) `GET /activate_tab?name=Render%20Graph`** → `200`,
`{"activated_tab":"Render Graph","success":true}`.

**c) `GET /get_swapchain`** → `200`, real PNG, showing the "Render Graph" tab
active with its Offscreen Regime pass table populated (screenshot captured
and visually confirmed inline during this session — table shows
`AtmosphereTransmitt...` (0.12 ms), `AtmosphereMultiScatt...` (1.16 ms),
`AtmosphereSkyViewL...` (0.39 ms, x2 rows), `AtmosphereAerialPers...` (0.97 ms,
x2 rows + one 0.01 ms debug-slice row), `RenderOpaque` (0.01 ms, x2 rows —
GameView + SceneView), `DrawSkyBackground` (0.02 ms, x2 rows),
`RenderTransparent` (0.01 ms, x2 rows) — GPU-Driven Batches section correctly
shows "No GPU-driven-eligible batch is live this frame", Plugin Render
Features section shows `[PostComposite] DemoRenderFeatureV2 - priority 0,
blend Replace` / `[PreUI] DemoRenderFeatureV2Second - priority 0, blend
AlphaOver").

**d) Manual cross-check (THE actual proof the "one source of truth" goal was
genuinely met)**: picked the `RenderOpaque` (GameView row) pass visible in the
screenshot's Offscreen Regime table — GPU Time column reads **`0.01 ms`**,
Draws **`0`**, Tris **`0`**. A second, immediately-following `GET
/render_graph` call's JSON body contains, for `"name":"RenderOpaque"`,
`"view_scope":"GameView"`: `"gpu_timing_text":"0.01 ms"`,
`"draw_call_count":0`, `"triangle_count":0` — **an exact match**, confirmed
field-by-field (the small GPU-timing-value drift between the FIRST
`/render_graph` call (`0.010833333068847656` ms) and the SECOND, later one
taken right before the screenshot comparison (`0.009166666442871094` ms) is
expected, normal frame-to-frame GPU timestamp measurement jitter — both
independently round to the same displayed `"0.01 ms"` the panel itself shows,
which is the actual thing being cross-checked).

**e) `GET /get_logs?limit=50`** → `200`, 13 total log entries this session,
every single one a pre-existing, already-documented warning/info line
(plugin static-CRT-linkage risk notice, 6x plugin-load Info lines, "2 loaded
plugins implement IRenderFeatureModule_v1" Warning, NetworkServer "listening"
Info, EditorHost construction Info, 3x pre-existing GPU-timing-slot-budget-
exhausted Warnings for `RenderFeatureCompositor_Scene_SeedCopy`/
`DemoRenderFeatureV2_Scene_Blend`/`DemoRenderFeatureV2Second_Scene_Blend`) —
**zero new/unexpected warning or error text** introduced by this phase's own
new publish call or new route.

`stop_app_background` (PID 12008) called afterward — confirmed terminated
successfully.

### 4. `git_status` re-confirmed immediately before this report/commit

```
modified:   src/Application/FrameCaptureBridge.cpp
modified:   src/Application/FrameCaptureBridge.h
modified:   src/Editor/EditorHost.cpp
modified:   src/Network/NetworkRoutes.cpp
modified:   src/Network/NetworkRoutes.h
modified:   src/Network/NetworkServer.cpp
modified:   tests/Network/NetworkRoutesTests.cpp
```

Exactly the files this phase's own plan says it may touch — nothing else.
`RenderGraphPanel.cpp` was NOT touched (Phase 3 already finished the panel's
own migration); `docs/conventions/networking.md` was NOT touched (Phase 5's
job).

## What this phase deliberately did NOT do (unchanged from the plan)

- Did not add any query-parameter filtering to `GET /render_graph` (Locked
  Design Decision #8).
- Did not touch `RenderGraphPanel.cpp` itself.
- Did not write the final `docs/conventions/networking.md` bullet (PHASE5).
- Did not run a full clean build or full `ctest` pass (Workflow Rule 1 —
  Phase 5 only).

## Ambiguities encountered

None requiring `ask_questions`. Every fact needed (the exact
`PublishTextureList`/`GetPublishedTextureList` locking shape to mirror, the
exact `EditorHost.cpp` `BuildUI` block/`renderFeatureEntries` scope, the exact
`RegisterListTexturesRoute()` template to mirror for the new route, the exact
status-code mapping — `503` on a null bridge, `200` otherwise, no other code
path) was directly confirmed by reading the real source and this phase's own
detailed plan before writing any code. One real, self-caught-and-fixed
mistake (not an ambiguity): the first draft of one new test wrongly assumed a
bare, hand-fabricated `RenderGraphMetadata` would already carry
`BuildRenderGraphMetadata()`'s own populated regime-name strings — caught by
actually running the new test before considering it done, fixed with an
explanatory comment.
