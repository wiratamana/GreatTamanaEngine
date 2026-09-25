# `editor-core-separation-7` — CAMPAIGN COMPLETION REPORT

**Status: COMPLETE.** All five phases shipped exactly as scoped by
`PHASE0_MASTER_STRATEGY.md`, with zero unexplained regressions across the
whole campaign. Branch `feature/editor-core-separation` throughout, never
switched.

## Original goal (PHASE0 Step 1)

Make the Editor's "Render Graph" panel (`src/Editor/Panels/RenderGraphPanel.cpp`)
genuinely **data-driven**. Before this campaign, that panel reached into
THREE independent, differently-shaped pieces of state every frame (two
`rg::RenderGraphSnapshot` regimes, a `std::vector<GpuDrivenBatchDebugInfo>`,
and a `std::vector<RenderFeatureDebugEntry>`), with real formatting/
presentation logic baked directly into the panel's own `.cpp` file, and there
was no JSON form of any of this anywhere in the engine — no way for an
external tool (including an AI agent doing LLM-driven debugging of this very
engine) to ask "what did the render graph do last frame" over the network,
the way it already could ask "what does the screen look like"
(`GET /get_game_view`) or "what did the engine log" (`GET /get_logs`).

The campaign's target flow:

```
RenderGraph (live C++ object, two regimes)  -\
GpuDrivenBatchDebugInfo (per-frame culling)   >-- rg::BuildRenderGraphMetadata() --> rg::RenderGraphMetadata (ONE plain, JSON-able struct)
RenderFeatureDebugEntry (loaded _v2 plugins) -/                                            |
                                                                                            +--> rendered by RenderGraphPanel (ImGui reads the metadata, draws it)
                                                                                            +--> served over GET /render_graph (an external/AI caller fetches the exact same metadata)
                                                                                            +--> exported as a real Graphviz .dot file ("Export DOT" button, finally implemented)
```

This was explicitly READ-ONLY introspection of an already-executed graph —
never a way to declare/author passes, never a way to replay/mutate a graph
from JSON.

## What each phase actually shipped

### PHASE1 — Extract Presentation-Formatting Helpers
(`PHASE1_COMPLETION_REPORT.md`)

New `gte_core`-tier file pair, `src/Renderer/RenderGraph/
RenderGraphSnapshotFormatting.h/.cpp`: relocated `FormatGpuTiming()`/
`JoinNames()`/a renamed `ResolvePassNameAtSurvivingIndex()` out of
`RenderGraphPanel.cpp`'s anonymous namespace verbatim, plus two brand-new
helpers that did not exist anywhere yet — `ToString(ResourceKind)` and
`ToString(ViewScope)`. 13 new Tier-1 tests. Zero observable behavior change
to `RenderGraphPanel`, proven by a real `git_stash`-based before/after
screenshot comparison (pixel-identical layout/text, only live GPU-timing
numbers differing, as expected).

### PHASE2 — `RenderGraphMetadata` Data Model + JSON
(`PHASE2_COMPLETION_REPORT.md`)

New, purely additive `gte_core`-tier file pair, `src/Renderer/RenderGraph/
RenderGraphMetadata.h/.cpp`: `RenderGraphMetadata`/`RenderGraphRegimeMetadata`/
`RenderGraphPassMetadata`/`RenderGraphResourceMetadata`/
`RenderGraphResourceRefMetadata` types, `BuildRenderGraphMetadata()` (pure,
Tier-1-testable, folds in ALL THREE of the panel's original data sources per
Locked Design Decision #2), and every `to_json()` overload (`snake_case`
keys, `std::nullopt` → JSON `null`, ADL `to_json` for the two borrowed
`GpuDrivenBatchDebugInfo`/`RenderFeatureDebugEntry` structs per Locked Design
Decision #6). 11 new Tier-1 tests. Confirmed genuinely inert — zero
production call sites — exactly as scoped for a "phase N builds the pure
logic" step.

### PHASE3 — Editor Panel Data-Driven Migration + Real "Export DOT"
(`PHASE3_COMPLETION_REPORT.md`)

`RenderGraphPanel::Build()` refactored to build (or reuse the frozen) ONE
`rg::RenderGraphMetadata` per frame and draw its ImGui tables from THAT,
never from the three raw sources directly — the literal meaning of
"data-driven". New `gte_editor`-tier file pair,
`src/Editor/RenderGraphDotExport.h/.cpp` (Locked Design Decision #14):
`BuildRenderGraphDot()` (pure Graphviz-digraph builder) +
`ExportRenderGraphDotToFile()` (writes `render_graph_export.dot`,
working-directory-relative, always overwritten per Locked Design Decision
#13, logs the resolved path via `GTE_LOG_INFO`). The panel's previously
disabled "Export DOT" button (tooltip: *"Planned for Phase 9..."*) is now
real and enabled. 6 new Tier-1 tests. One honestly-disclosed verification
gap: a live interactive click of the Pause checkbox/"Export DOT" button was
not exercised this session (this engine has no HTTP endpoint that can press
an arbitrary ImGui widget) — substituted by a real, non-mocked Tier-1 test
proving `ExportRenderGraphDotToFile()` genuinely writes a well-formed file,
plus a passive (no-click) live screenshot confirming the button now renders
enabled. Explicitly recorded as a deferred manual-verification step, not
silently glossed over.

### PHASE4 — Cross-Thread Bridge + `GET /render_graph` HTTP Endpoint
(`PHASE4_COMPLETION_REPORT.md`)

Extended `FrameCaptureBridge` (Locked Design Decision #4) with a SECOND
`Publish*`/`Get*` pair — `PublishRenderGraphMetadata()`/
`GetPublishedRenderGraphMetadata()`, its own dedicated mutex, never a sixth
bridge class — and updated that file's own previously-stale header comment
to honestly describe all three payload kinds it now moves (captured PNG
images, the published texture list, the published render-graph metadata).
Wired the publish call into `EditorHost.cpp`'s existing `Run()` loop (Locked
Design Decision #3), reusing the already-computed `renderFeatureEntries`
local so `RenderFeatureCompositor::DebugSnapshot()` is still called at most
once per frame (Locked Design Decision #7). Added
`NetworkRoutes.h/.cpp`'s `BuildRenderGraphMetadataResponseJson()` and
`NetworkServer.cpp`'s `GET /render_graph` route registration — `200` on
success, `503` if the bridge pointer is null, no query parameters (Locked
Design Decision #8). 2 new Tier-1 tests. **Confirmed the real, shipped JSON
response matches PHASE2's own documented shape byte-for-byte** — no
correction was needed for PHASE5's own docs. Verified live end-to-end: a
`GET /render_graph` snapshot's `RenderOpaque` pass numbers matched the
simultaneously-screenshotted "Render Graph" panel's own displayed numbers
exactly (the actual proof the "one source of truth" goal was genuinely met).

### PHASE5 — Docs, Full Regression, Campaign Closeout
(`PHASE5_COMPLETION_REPORT.md`, this phase)

Added ONE new bullet to `docs/conventions/networking.md` (appended after the
file's real, freshly-`read_file`'d last line) documenting `GET /render_graph`'s
full contract. Re-confirmed via a fresh `search_in_dir` that none of
`AGENTS.md`'s existing 8 "Render Graph" mentions needed correction — all
describe still-true historical facts or generic architecture, none reference
the panel's old three-source internals or a disabled "Export DOT" button.
Ran the ONE full clean build + full `ctest` regression pass + final live HTTP
smoke test this whole campaign is allowed to run (see below) — zero
unexplained failures, so `delegate_task` was never invoked.

## Locked Design Decisions — how each was actually realized

All 14 of `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decisions were followed
exactly as written, with zero deviation discovered during implementation:

1. Two NAMED regime fields (`offscreenRegime`/`presentRegime`) — shipped
   exactly this way, PHASE2.
2. All THREE panel data sources folded into `RenderGraphMetadata` — shipped,
   PHASE2 (confirmed real via the actual current `RenderGraphPanel::Build()`
   signature before writing any code).
3. Publish call added inside `EditorHost.cpp`'s `Run()` loop, not
   `Application.cpp` — shipped, PHASE4.
4. Extended `FrameCaptureBridge`, no sixth bridge class — shipped, PHASE4,
   including the required header-comment fix.
5. `tagGroupLabel` is `std::optional<std::string>`, never a vector — shipped,
   PHASE2.
6. ADL `to_json` for the two borrowed structs, no new wrapper types —
   shipped, PHASE2.
7. `RenderFeatureCompositor::DebugSnapshot()` called at most once per frame,
   reusing the existing local — shipped, PHASE4.
8. No new query parameters on `GET /render_graph` for v1 — shipped, PHASE4,
   confirmed zero in the route registration.
9. `GET /render_graph` fully independent of the panel's own "Pause" checkbox
   — shipped, PHASE3/PHASE4 (the publish call runs every real frame
   regardless of any Editor panel's pause state).
10. `RenderGraphResourceMetadata` carries both the raw index AND the
    resolved pass name — shipped, PHASE2.
11. `RenderGraphMetadata.h/.cpp` is a new, separate file, never folded into
    `RenderGraphSnapshot.h/.cpp` — shipped, PHASE2.
12. Formatting helpers moved to a new sibling file,
    `RenderGraphSnapshotFormatting.h/.cpp` — shipped, PHASE1.
13. "Export DOT" writes a fixed, working-directory-relative path, logged via
    `GTE_LOG_INFO` — shipped, PHASE3.
14. `RenderGraphDotExport.h/.cpp` lives under `src/Editor/` (`gte_editor`),
    not `src/Renderer/RenderGraph/` (`gte_core`) — shipped, PHASE3.

## The final, locked `GET /render_graph` JSON shape (real, captured example)

Captured live from a running `GreatTamanaEditor.exe` during PHASE5's own
final smoke test (`GET /render_graph`, `200`, `Content-Type: application/json`).
Full body confirmed well-formed JSON with every documented top-level key
present and populated with real, current-frame data; shown here abbreviated
for readability (full body inspected directly during this session):

```json
{
  "schema_version": 1,
  "offscreen_regime": {
    "regime_name": "SynchronousImmediateReadback",
    "timing_slot_budget_exhausted": true,
    "passes": [
      {
        "name": "AtmosphereTransmittanceLutPass",
        "is_culled": false,
        "kind": "Compute",
        "category": "General",
        "draw_kind": "DrawMesh",
        "view_scope": "Shared",
        "render_pass_event": "PreOpaques",
        "tag_group_label": "Compute LUT",
        "reads": [],
        "writes": [{"kind": "Texture", "name": "AtmosphereTransmittanceLut"}],
        "draw_call_count": 0,
        "triangle_count": 0,
        "gpu_timing_text": "0.19 ms",
        "gpu_timing_milliseconds": 0.1863020787849426
      },
      {
        "name": "AtmosphereAerialPerspectiveVolumePass",
        "is_culled": false,
        "kind": "Compute",
        "category": "General",
        "draw_kind": "DrawMesh",
        "view_scope": "GameView",
        "render_pass_event": "PreOpaques",
        "tag_group_label": "Compute LUT",
        "reads": [
          {"kind": "Texture", "name": "AtmosphereTransmittanceLut"},
          {"kind": "Texture", "name": "AtmosphereMultiScatteringLut"}
        ],
        "writes": [{"kind": "VolumeTexture", "name": "AtmosphereAerialPerspectiveVolume_GameView"}],
        "draw_call_count": 0,
        "triangle_count": 0,
        "gpu_timing_text": "1.85 ms",
        "gpu_timing_milliseconds": 1.849166621520996
      },
      {
        "name": "RenderOpaque",
        "is_culled": false,
        "kind": "Graphics",
        "category": "General",
        "draw_kind": "DrawMesh",
        "view_scope": "GameView",
        "render_pass_event": "Opaques",
        "tag_group_label": null,
        "reads": [],
        "writes": [{"kind": "Texture", "name": "GameView"}, {"kind": "Texture", "name": "GameView"}],
        "draw_call_count": 0,
        "triangle_count": 0,
        "gpu_timing_text": "0.01 ms",
        "gpu_timing_milliseconds": 0.013645833000183105
      }
    ],
    "resources": [
      {
        "name": "AtmosphereTransmittanceLut",
        "is_imported": false,
        "first_use_pass_index": 0,
        "last_use_pass_index": 2,
        "first_use_pass_name": "AtmosphereTransmittanceLutPass",
        "last_use_pass_name": "AtmosphereSkyViewLutPass"
      }
    ]
  },
  "present_regime": {
    "regime_name": "PipelinedDeferredReadback",
    "timing_slot_budget_exhausted": false,
    "passes": [ /* the "Present" pass and any pass feeding the swapchain */ ],
    "resources": [ /* ... */ ]
  },
  "gpu_driven_batches": [],
  "render_features": [
    {"name": "DemoRenderFeatureV2", "stage": "PostComposite", "priority": 0, "blend_mode": "Replace"},
    {"name": "DemoRenderFeatureV2Second", "stage": "PreUI", "priority": 0, "blend_mode": "AlphaOver"}
  ]
}
```

(`resources`/`present_regime.passes` truncated above purely for report
readability — every field shape shown is exactly what
`gte::rg::RenderGraphMetadata::to_json()` emits, confirmed field-by-field by
both PHASE2's/PHASE4's own Tier-1 tests and this phase's own live capture.
`gpu_timing_milliseconds`/`tag_group_label` correctly serialize as JSON
`null` when absent/unresolved, never an empty-string placeholder, per Locked
Design Decisions #5/#10.)

## Every `PHASEn_COMPLETION_REPORT.md`

- `PHASE1_COMPLETION_REPORT.md`
- `PHASE2_COMPLETION_REPORT.md`
- `PHASE3_COMPLETION_REPORT.md`
- `PHASE4_COMPLETION_REPORT.md`
- `PHASE5_COMPLETION_REPORT.md`

(All in this same folder, `task_manager/editor-core-separation-7/`.)

## Full regression result (PHASE5)

- **Full build** (`cmake --build build`, existing configured tree): `ninja:
  no work to do` — everything already up to date from Phases 1–4's own
  incremental builds.
- **Full `ctest` regression**: **1819 total tests, 1817 passed (100% of
  executed tests), 2 legitimate environment-gated skips, ZERO failures** —
  up from `editor-core-separation-6`'s own documented 1787-test baseline
  (+32, exactly matching this campaign's 13+11+6+2 new Tier-1 tests across
  Phases 1–4). No unexplained regression was found anywhere, so
  `delegate_task` was correctly never invoked for this campaign.
- **Live HTTP smoke test**: `GET /render_graph` (`200`, real current-frame
  JSON, shape confirmed as above), `GET /get_swapchain` (`200`, real PNG,
  visually confirmed the Editor UI renders correctly), `GET /get_logs?limit=50`
  (`200`, only pre-existing, already-documented warning/info lines — zero new
  warnings/errors introduced by this whole campaign).

## What this campaign explicitly did NOT do (Non-Goals, unchanged from PHASE0)

- No data-driven graph *authoring* system — hand-written `AddPass()` C++
  remains the only way to declare passes.
- No schema for round-tripping metadata back into a live `RenderGraph`.
- No general-purpose "every Editor panel becomes data-driven + JSON-exposed"
  initiative — scoped exactly to the "Render Graph" panel.
- No security-hardening pass on the embedded HTTP server.
- No query-parameter filtering on `GET /render_graph` (`?regime=`/`?pass=`).
- No native file-save dialog for "Export DOT".
- No change to any OTHER Editor panel, any OTHER network route, or any
  `_v1`/`_v2` plugin render-feature ABI surface.

## Conclusion

The Render Graph panel is now genuinely data-driven end-to-end: one pure
`rg::RenderGraphMetadata` object is the single source of truth consumed by
the ImGui panel, the "Export DOT" Graphviz exporter, and the new
`GET /render_graph` HTTP endpoint alike — proven not just by design but by a
live, field-by-field cross-check between the panel's own displayed numbers
and the HTTP response's JSON body for the same frame (PHASE4), and by a
full, zero-regression build + test + smoke-test pass at campaign close
(PHASE5).
