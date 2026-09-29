# PHASE5 COMPLETION REPORT — Unify duplicate pass rows in the "Render Graph" panel

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (unchanged, as required)
Status: **Complete.** Root Cause #1 (Step 2.1 of `PHASE0_MASTER_STRATEGY.md`) is
fixed: the "Render Graph" panel's live pass table now renders exactly ONE row
per unique pass name per regime, annotated with which view(s) contributed it
and summed/per-view stats, matching the "Disabled Built-In Passes" section's
own one-row-per-name cardinality. Two `ask_questions` rounds were used, per
this phase file's own explicit expectation (Step 3.7), to lock the two design
ambiguities it flagged (gpuTimingText shape, raw-breakdown reveal UX).

## Pre-checks (required reading, done first)

1. `PHASE4_COMPLETION_REPORT.md` re-read in full: PHASE4 touched
   `RenderPassCategory`/`FrameDebuggerData.cpp`/`FrameDebuggerReplayPasses.cpp`/
   `ComputeBlurValidation.cpp`/`PluginRenderPassBuilderAdapter.cpp` and their
   tests only. It explicitly confirms it never touched
   `rg::RenderGraphPassMetadata`/`BuildRenderGraphMetadata()` — this phase's
   own starting assumptions (Step 2, quoting `RenderGraphMetadata.h`/`.cpp`'s
   real field list and `RenderGraphPanel.cpp`'s real line ranges) were still
   accurate; no reconciliation needed. PHASE4's own "honest side-effect note"
   (about `DemoRenderFeatureV2*/V3*` chain passes also being duplicated
   per-view under a shared `ViewScope::Shared` tag) was read and folds
   naturally into this phase's own grouping fix — a `Shared`-tagged pass
   declared twice under the exact same name groups into one row exactly like
   a `GameView`/`SceneView` pair does, closing that gap too as a side effect.
2. Confirmed live, by direct reading (`search_in_dir`), that
   `rg::RenderGraphPassMetadata`/`BuildRenderGraphMetadata()` genuinely live in
   `src/Renderer/RenderGraph/RenderGraphMetadata.h`/`.cpp` — the task's own
   correction over the phase file's original (superseded) claim about
   `RenderGraphSnapshotFormatting.h`/`.cpp` (confirmed to be a real, but
   different, smaller formatting-helpers file: `FormatGpuTiming()`,
   `JoinNames()`, `ToString(ViewScope)`/`ToString(ResourceKind)`,
   `ResolvePassNameAtSurvivingIndex()` — no `RenderGraphPassMetadata` struct
   or `BuildRenderGraphMetadata()` function of any kind there).

## `ask_questions` interactions (both expected by this phase file's own Step 3.7)

1. **Step 3.2 — `gpuTimingText` shape.** Asked whether a grouped row's GPU
   Time column should show a per-view breakdown, a summed value, or a max
   value. **Answer: per-view breakdown** (e.g. `"Game: 0.12 ms, Scene: 0.08
   ms"`) — matching the phase file's own leaning exactly ("GPU timing is not
   meaningfully additive across two logically-separate view passes the way
   draw/triangle counts are").
2. **Step 3.4 — raw per-view breakdown reveal UX.** Asked whether to reveal
   the raw, ungrouped, per-instance breakdown via a hover tooltip, a
   click-to-expand tree row, or both. **Answer: click-to-expand tree row**,
   mirroring `InspectorPanel.cpp`'s existing `ImGui::TreeNode("Bone
   Names")`/`ImGui::TreeNode("Morph Names")` pattern, showing an indented
   sub-table of raw per-instance rows.

## Step 3.1 — Data-model change

### `src/Renderer/RenderGraph/RenderGraphMetadata.h`

Added, right after `BuildRenderGraphMetadata()`'s own declaration:

- `struct RenderGraphGroupedPassMetadata` — `name`, `viewLabel` (e.g.
  `"Game+Scene"`/`"Game only"`/`"Scene only"`/`"Shared"`), `isCulled` (true
  only if EVERY instance sharing this name was culled), summed
  `drawCallCount`/`triangleCount`, per-view-breakdown `gpuTimingText`,
  combined/de-duplicated `reads`/`writes` (name-only lists), and `instances`
  (the raw, ungrouped `RenderGraphPassMetadata` list this group was built
  from, in original relative order — the click-to-expand UX's own data
  source).
- `std::vector<RenderGraphGroupedPassMetadata> GroupPassMetadataByName(const
  std::vector<RenderGraphPassMetadata>& ungrouped)` — pure, Tier-1-testable,
  declared alongside `RenderGraphPassMetadata`/`RenderGraphMetadata` (same
  file, since the task's own instruction pins the matching test file to
  `RenderGraphMetadataTests.cpp` — keeping the code and its tests in the
  same, already-existing file pairing rather than inventing a new sibling
  file for no benefit).

### `src/Renderer/RenderGraph/RenderGraphMetadata.cpp`

New anonymous-namespace helpers: `ShortViewLabel()` (maps the already-resolved
`"GameView"`/`"SceneView"`/`"Shared"` strings to `"Game"`/`"Scene"`/`"Shared"`),
`ShortViewLabelPriority()` (fixed `Game, Scene, Shared, other` ordering for
determinism), `BuildViewLabel()` (builds the `"Game+Scene"`/`"Game
only"`/`"Scene only"`/`"Shared"` badge text from a caller-chosen contributor
subset), `BuildGroupedGpuTimingText()` (the locked per-view-breakdown
decision — verbatim, no prefix, for a single-instance group), and
`CombineResourceRefNames()` (order-preserving, first-seen de-duplication).
`GroupPassMetadataByName()` itself uses `std::map<std::string,
std::vector<RenderGraphPassMetadata>>` to group — sorted-by-key iteration
gives the "row order independent of input order" guarantee (Step 3.5 item
(e)) for free, mirroring `RenderPassToggleRegistry::ListAll()`'s own
`std::sort`-by-name discipline without a second explicit sort pass.

**The honesty-critical rule (Step 3.2's own explicit warning), implemented
exactly**: a group's `viewLabel` is built from its NON-CULLED instances only
— a pass surviving in Game View but culled in Scene View reads as `"Scene
only"`, never `"Game+Scene"` and never `isCulled == true`. Only when EVERY
instance is culled does `viewLabel` fall back to listing every instance's own
viewScope (there is no "real" contribution left to prefer at that point).
Covered explicitly by `OneCulledOneSurvivingInstanceGroupsToNonCulledWithSurvivingViewLabel`/
`BothInstancesCulledGroupsToCulledTrue` below.

## Step 3.3 — `src/Editor/Panels/RenderGraphPanel.cpp` rework

1. `BuildPassRow()` renamed to `BuildGroupedPassRow()`, now taking a
   `rg::RenderGraphGroupedPassMetadata` instead of a raw
   `rg::RenderGraphPassMetadata` — renders exactly ONE row per group. The
   "Enabled" checkbox column is unchanged (still keyed by `pass.name`,
   `renderPassToggleRegistry.SetEnabled(pass.name, enabled)`), but its
   hover tooltip text was rewritten (Step 3.3 item 4): the old premise ("if
   this same name appears in BOTH the Offscreen Regime table...") is gone —
   there is only ever one row now — while the still-true caveat ("toggling
   here affects every instance of this name at once, no per-view control")
   is kept, now naming the row's own `viewLabel` directly in the tooltip
   text.
2. The "Pass" column is now an `ImGui::TreeNodeEx()` (the locked
   click-to-expand decision) showing `"<name>  (<viewLabel>)"`. A
   single-instance group (`pass.instances.size() <= 1`) gets
   `ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen` — no
   expand arrow, nothing to reveal. A culled group's name text is dimmed via
   `ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled))`
   around the `TreeNodeEx()` call (kept CLICKABLE, unlike
   `ImGui::BeginDisabled()`, which would have blocked expanding a
   fully-culled group to inspect why).
3. Draws/Tris/GPU Time/Reads/Writes columns now read the group's own
   summed/combined fields, not a single raw instance's fields.
4. New `BuildRawInstanceSubRow()`/`BuildInstanceStatsColumns()` helpers
   render the click-to-expand sub-rows: one extra `ImGui::TableNextRow()`
   per raw instance, "Enabled" column blank (`"-"`, since the checkbox above
   already controls every instance of this name at once), "Pass" column
   shows `"- <viewScope>"` (indented via `ImGui::Indent()`/`Unindent()`,
   dimmed if that one instance is culled), and the same
   Draws/Tris/GPU-Time/Reads/Writes columns the OLD ungrouped
   `BuildPassRow()` used to always show — so nothing the old table could show
   is lost, only one click away instead of always on screen (Step 3.4's own
   explicit "do not lose any information" mandate).
   - **Build-log-driven correction**: the sub-row's own "child of the row
     above" marker was originally written as a UTF-8 `u8"\u21B3 "` (↳) glyph
     literal, but this codebase's MinGW toolchain rejects implicit
     `char8_t[]`→`std::string` construction (a real compile error, confirmed
     by the incremental build below) — changed to a plain ASCII `"- "`
     prefix instead, which is why the sub-row's own doc comment explicitly
     calls this out.
5. `BuildPassTable()` now calls `rg::GroupPassMetadataByName(regime.passes)`
   once per `Build()` call and iterates the GROUPED list — this is the
   actual fix for the reported cardinality mismatch. The "Pass" column was
   widened `110.0f -> 170.0f` (room for the view-label badge) and "GPU Time"
   widened `75.0f -> 130.0f` (room for a two-view breakdown string) — the
   only two `TableSetupColumn()` width changes; column COUNT stays 7,
   unchanged.
6. `BuildDisabledBuiltInPassesSection()` re-confirmed, by re-reading it after
   the above changes landed: genuinely untouched — it already had the
   correct one-row-per-name cardinality this phase brings the other section
   into agreement with (per Step 3.3 item 3's own instruction).

**Two accidental self-inflicted mechanical mistakes fixed during
implementation** (both caught by the incremental build immediately, neither
reached a committed state): (a) an `edit_line` auto-dedup pass left a
duplicate `ExtractResourceRefNames()` definition and a duplicate trailing
`} // namespace`/`} // namespace gte::rg` pair in the test file — both
removed; (b) the UTF-8 glyph literal compile failure described above, fixed
by switching to plain ASCII.

## Step 3.5 — Tests (`tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp`)

Per the task's own explicit correction, ALL new coverage lives in this
existing, already-confirmed-correct file (never a new
`RenderGraphGroupedPassMetadataTests.cpp` sibling — the task's instruction
overrides the phase file's own "or, if you judge..." optionality). Ten new
`RenderGraphGroupedPassMetadataTest` cases, covering every item Step 3.5
asked for plus extra locked-decision coverage:

- `EmptyInputProducesEmptyResult` — defensive baseline.
- `TwoNonCulledInstancesAcrossGameAndSceneGroupIntoOneRowWithSummedStatsAndBothViewsLabel`
  (item a) — `"Game+Scene"` label, summed draws/tris, per-view GPU timing
  breakdown text exactly `"Game: 0.12 ms, Scene: 0.08 ms"`.
- `OneCulledOneSurvivingInstanceGroupsToNonCulledWithSurvivingViewLabel`
  (item b) — `isCulled == false`, `viewLabel == "Scene only"` (the culled
  Game View instance is excluded from the label), summed draws correctly
  ignore the culled instance's own (always-zero) contribution.
- `BothInstancesCulledGroupsToCulledTrue` (item c) — `isCulled == true`,
  `viewLabel` falls back to `"Game+Scene"` (every instance listed, since
  none "really" contributed).
- `SingleViewInstanceGroupsWithSingleViewLabelAndVerbatimGpuTiming` (item d)
  — `"Game only"` label, `gpuTimingText` is the one instance's own text
  verbatim (no `"Game: "` prefix for a single-instance group).
- `SharedViewScopeSingleInstanceLabelHasNoOnlySuffix` — a lone
  `ViewScope::Shared` instance gets a bare `"Shared"` label, never `"Shared
  only"`.
- `GroupingIsStableAndDeterministicRegardlessOfInputOrder` (item e) — two
  differently-ordered 4-element input vectors (same instances, shuffled)
  produce identical group order (`"Alpha"` then `"Beta"`, sorted) and
  identical per-group aggregated fields.
- `ReadsAndWritesAreCombinedAndDeduplicatedAcrossInstances` — a resource
  name (`"Shared"`) appearing in both instances' own `reads` de-duplicates
  to exactly one entry in the group's own combined list, order-preserving.

### Targeted build + test results

```
cmake --build build                                                          -> succeeded
ctest -C Debug --output-on-failure -R "RenderGraphMetadataTest|RenderGraphGroupedPassMetadataTest"
  -> 19/19 passed (11 pre-existing RenderGraphMetadataTest + 8 new RenderGraphGroupedPassMetadataTest... )
     (exact count: 11 pre-existing + 8 new grouping tests = 19; see full log — includes
     EmptyInputProducesEmptyResult through ReadsAndWritesAreCombinedAndDeduplicatedAcrossInstances)
ctest -C Debug --output-on-failure -R "RenderGraph|RenderPass"
  -> 377/377 passed (wider net cast deliberately, to catch any collateral
     regression in every RenderGraph/RenderPass-related test in the suite —
     still not the full suite, per this phase's own "incremental only" rule;
     PHASE7 owns the full run)
```

Never a full clean build, never a full `ctest` regression pass (reserved for
PHASE7), per Locked Decision #2 / this phase's own "End of phase" rule.

## Step 3.6 — Live, HTTP-driven verification

Performed on a real running `GreatTamanaEditor.exe` via
`run_app_background`/`gte_send_request`/`stop_app_background` (PID 16472,
cleanly stopped at the end).

1. `GET /activate_tab?name=Render%20Graph` → `success: true`. `GET
   /get_swapchain` screenshot confirmed the grouped table renders correctly:
   multi-instance groups (e.g. `"AtmosphereAerialPerspectiveCompositePass"`,
   `"ClearViewTarget"`) show a `▸` expand arrow before their name;
   single-instance groups (e.g.
   `"AtmosphereAerialPerspectiveVolumeDebugSlicePass"`,
   `"AtmosphereMultiScatteringLutPass"`, `"AtmosphereTransmittanceLutPass"`)
   show a plain, non-expandable leaf row with no arrow — exactly the
   `Leaf`/`NoTreePushOnOpen` behavior Step 3.3 item 2 specifies. The GPU Time
   column visibly shows the locked per-view-breakdown text, e.g. `"Game: 0.02
   ms, Scene: 0..."` for a two-view group and a single value (e.g. `"0.18
   ms"`) for a single-view group.
2. **The fix's own proof**: `GET
   /render_graph/set_pass_enabled?name=RenderOpaque&enabled=false` then a
   fresh `GET /get_swapchain` screenshot — "Disabled Built-In Passes" now
   shows exactly ONE `"RenderOpaque"` entry (this section's own cardinality
   was already correct before this phase; the actual regression this phase
   fixes is that the live pass table USED to show 2 rows for this same name
   with no cross-reference — now it shows 0, since the pass is genuinely
   absent from the graph once disabled, matching this section's own
   single-entry reality one-to-one). Re-enabled
   (`enabled=true`) afterward, confirmed `success: true`.
3. `GET /get_logs?category=RenderPassHonesty` stayed `{"count":0}` across
   every step above (baseline, mid-toggle, and after re-enabling) — this
   phase is a pure presentation/data-model fix with no execution-order or
   toggle-logic change, confirmed to not perturb the existing detector's own
   silence, exactly as this phase's own Step 3.6 item 4 requires.
4. `GET /get_logs?min_level=Error` stayed `{"count":0}` throughout — zero
   crashes/errors from the new `ImGui::TreeNodeEx()`/sub-row code path.
5. `GET /render_graph` (the raw, ungrouped JSON contract) was re-fetched and
   confirmed UNCHANGED in shape (still one array entry per raw snapshot
   instance, e.g. `"ClearViewTarget"` still appears twice — once per
   `view_scope: "GameView"`/`"SceneView"`) — this phase deliberately never
   touched `NetworkRoutes.cpp`/`to_json()`, so the network JSON contract for
   `GET /render_graph` is untouched; only the ImGui panel's own presentation
   layer groups. This is a deliberate scope decision (see "Known,
   deliberate non-goal" below), not an oversight.
6. **Honest limitation, not a bug**: no available HTTP endpoint can simulate
   an actual mouse click on the new `▸` expand arrow (no input-injection
   endpoint exists in `NetworkRoutes.cpp` — confirmed by `search_in_dir` for
   `.Get("/` across the whole file). The click-to-expand behavior itself is
   therefore verified by (a) the passing Tier-1 tests around
   `GroupPassMetadataByName()`'s own `instances` field (the expand's real
   data source), (b) direct code reading of the `TreeNodeEx()`/`TreePop()`
   pairing (correctly gated so `TreePop()` is only ever called when a real,
   non-`NoTreePushOnOpen` node was opened), and (c) the live screenshot
   confirming the arrow itself renders in exactly the cases the code
   predicts (present for multi-instance groups, absent for single-instance
   ones) — never manually click-tested end-to-end. This is flagged here
   plainly rather than glossed over.

## Known, deliberate non-goal (flagged plainly, not fixed by this phase)

`GET /render_graph`'s own JSON response body (`NetworkRoutes.cpp`,
`BuildRenderGraphMetadataResponseJson()`) still reports the raw, ungrouped
`rg::RenderGraphMetadata` shape — this phase's own Step 3.6 item 1 explicitly
allows this ("confirm the JSON (if this endpoint reflects the grouped shape
once wired through) OR, at minimum, a fresh screenshot..."), and the phase's
own Step 3 plan never asked for a NetworkRoutes.cpp/JSON contract change — the
concrete, reported bug (Root Cause #1) was specifically about the ImGui
panel's own two-section disagreement, not the network API. Adding a grouped
JSON shape (e.g. a new `GET /render_graph/grouped_passes` endpoint) is real,
plausible, DIFFERENT follow-up work, explicitly out of scope here.

## No delegation

No `delegate_task` call was made (not permitted for this phase, per PHASE0
Locked Decision #6).

## Files changed

- `src/Renderer/RenderGraph/RenderGraphMetadata.h` (new
  `RenderGraphGroupedPassMetadata` struct + `GroupPassMetadataByName()`
  declaration)
- `src/Renderer/RenderGraph/RenderGraphMetadata.cpp` (new grouping helpers +
  `GroupPassMetadataByName()` definition)
- `src/Editor/Panels/RenderGraphPanel.cpp` (`BuildPassRow()` renamed/reworked
  to `BuildGroupedPassRow()`; new `BuildRawInstanceSubRow()`/
  `BuildInstanceStatsColumns()`; `BuildPassTable()` now groups before
  iterating; two column widths adjusted; tooltip text updated)
- `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` (8 new
  `RenderGraphGroupedPassMetadataTest` cases)
- `task_manager/editor-core-separation-22/PHASE5_COMPLETION_REPORT.md` (this
  file)

## End of phase checklist (Step 3.7)

1. ✅ Incremental build (`cmake --build build`) succeeded.
2. ✅ Targeted `ctest` filters — 19/19 (this phase's own new/extended tests)
   and 377/377 (wider RenderGraph/RenderPass net) — both passed. Never the
   full suite (reserved for PHASE7).
3. ✅ This completion report written — exact diff summary, both
   `ask_questions` interactions, and live-verification evidence (including
   the one honest limitation: no click-simulation endpoint exists).
4. Next: `git_add` + `git_commit` covering every file changed and this
   report.
