# PHASE8 — Docs, Full Regression, Campaign Closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST). Also read every
`PHASE1`..`PHASE7` completion report before starting — this phase's own
regression pass must confirm every one of them still holds true together,
not just individually.

## Step 1: The Goal

Update this repo's own conventions documentation to describe the new `_v2`
render-feature system and the new `IPluginCapabilityOrchestrator` pattern,
run the ONE full clean build + full `ctest` pass + live HTTP smoke test this
whole campaign is allowed to run, confirm every pre-existing consumer of the
OLD (`_v1`) behavior still works identically, and close out the campaign.

## Step 2: The Situation

`docs/conventions/plugin-architecture.md` currently documents the `_v1`-era
architecture (`IPluginModule`/`QueryCapability`, `IRenderFeatureModule_v1`,
`IEditorPanelModule_v1`, the shared-CRT caveat, the "2+ render-feature
plugins: last write wins" limitation this whole campaign fixes). Read it in
full before editing — confirm exactly which sections describe the
now-superseded limitation so they can be corrected rather than left stale
and misleading.

`plugins/gte_plugin_abi/PublicSurface.md` already has PHASE1's own addition
(this campaign's Step 3.5 from `PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md`)
— this phase adds the REMAINING additions (nothing else crossed the plugin
ABI boundary in PHASES 2-7, since `IPluginCapabilityOrchestrator`/
`RenderFeatureCompositor` are `gte_core`-internal, never plugin-ABI-facing
— confirm this is still true by re-reading every new file this campaign
added under `plugins/gte_plugin_abi/` vs. `src/Core/Plugins/` before writing
this phase's own `PublicSurface.md` update, since an incorrect "this crossed
the ABI" claim would be actively misleading).

## Step 3: The Plan

### Step 3.1 — `docs/conventions/plugin-architecture.md` updates

- Add a new section (or extend the existing render-feature section)
  documenting `_v2`: `IRenderFeatureModule_v2`, `GtePluginRenderFeatureDescriptor`
  (`stage`+`priority`+`blendMode`), `IPluginRenderPassBuilder_v2`'s 3 fixed
  operations, and the real per-plugin-private-target compositing model —
  contrasted explicitly against `_v1`'s own "shared handle, last write
  wins" limitation (which the doc must now describe as "the LEGACY,
  still-supported `_v1` path — see `_v2` for real multi-plugin
  compositing," not simply delete the old description outright, since `_v1`
  is still real, working, supported code).
  Explicitly state which `RenderFeatureStage` values are actually wired
  (`PostComposite`, `PreUI`) vs. declared-but-refused
  (`PreOpaque`/`PostOpaque`/`PostTransparent`), and why (PHASE0 Locked
  Design Decision #1).
- Add a new section documenting `IPluginCapabilityOrchestrator`
  (`src/Core/Plugins/IPluginCapabilityOrchestrator.h`) as the general,
  reusable mechanism for "Core reacts to a newly-loaded plugin capability" —
  list its 2 existing built-in implementations
  (`LegacyRenderFeatureOrchestrator`, `EditorPanelCapabilityOrchestrator`)
  plus the new `RenderFeatureCompositor`, and state plainly that a future
  capability kind should add a THIRD orchestrator here, never hand-edit
  `Core::LoadPlugins()`'s or `Core::RegisterOffscreenRenderPipelineProviders()`'s
  own bodies again.
- Update `AGENTS.md`'s own "Plugin Architecture" section (if it references
  the old "2+ render-feature plugins: last write wins, only a warning is
  logged" limitation by name) to point at the new `_v2` system instead —
  confirm via `search_in_dir` whether `AGENTS.md` actually mentions this at
  all before assuming an edit is needed.

### Step 3.2 — `plugins/gte_plugin_abi/PublicSurface.md` final pass

Confirm the PHASE1 entry (already added) is accurate and complete against
the FINAL, real, shipped shape of every `_v2` ABI file (re-read
`RenderFeatureDescriptor.h`, `IPluginRenderPassBuilder_v2.h`,
`IRenderFeatureModule.h`'s `_v2` addition as they ACTUALLY ended up after
PHASES 4-6's own real implementation work, which may have refined field
names/method signatures slightly from PHASE1's own initial sketch) — correct
the PHASE1 bullet in place if anything drifted, rather than leaving a
now-inaccurate description.

### Step 3.3 — Full regression pass (the ONE time this whole campaign runs it)

1. Full clean build: confirm with the user/via `ask_questions` whether a
   genuinely clean rebuild (`cmake --build build` after deleting/reconfiguring
   `build/`) is wanted, or whether the accumulated incremental state from
   Phases 1-7 is trusted as-is — this repo's own prior campaigns
   (`editor-core-separation-5` PHASE5) treat a fresh `cmake --build build`
   over the EXISTING, already-configured `build/` tree as sufﬁcient
   ("the ONE full clean build... this whole campaign is allowed to run"
   does not necessarily mean re-running `cmake` configure from scratch —
   confirm via `ask_questions` if genuinely unsure).
2. Full regression test: `cd /d
   C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
   --output-on-failure`. If ANY test fails, root-cause it — if the failure
   is genuinely caused by this campaign's own changes, use `delegate_task`
   to fix it (Workflow Rule 7's one stated exception for Phase 8 alone,
   `PHASE0_MASTER_STRATEGY.md`) rather than leaving it broken or silently
   working around it.
3. Live HTTP smoke test, confirming EVERY item below still behaves
   IDENTICALLY to before this whole campaign started (re-derive each exact
   expected value from the relevant earlier phase's own completion report,
   never guess):
   - `GET /get_logs?limit=100` after a fresh `POST /clear_logs` + engine
     restart — the exact `_v1` multi-plugin warning text (PHASE2 Step 2)
     still appears unchanged.
   - `GET /list_tabs` — the exact panel name list/order (PHASE3's own
     verification) still matches.
   - `GET /get_game_view` with ONLY the ORIGINAL 4 demo plugins + the 2 new
     PHASE6 `_v2` demo plugins loaded (the final, permanent `plugins/`
     folder state) — describe and screenshot the ACTUAL final composited
     result (this is expected to show the PHASE6 `_v2` compositing result,
     per whatever `_v1`-vs-`_v2` interaction PHASE6's own completion report
     already documented and explained — confirm it is still the SAME
     documented interaction, not a newly-different one that would indicate
     a regression introduced by PHASES 7-8's own edits).
   - The "Render Graph" panel's new "Plugin Render Features" section
     (PHASE7) still lists both `_v2` demo plugins correctly.
4. `tools/ci/gte_plugin_abi_handshake_probe` and
   `tools/ci/gte_plugin_isolation_probe` (if their own dedicated inner build
   folders still exist from prior campaigns — re-create per their own
   `CMakeLists.txt` if missing, mirroring `editor-core-separation-5`'s own
   established precedent for this) — confirm `LoadedModuleCount()` now
   correctly reads **6** (4 original + 2 new PHASE6 `_v2` plugins), and that
   the exact count of modules answering
   `QueryCapability(kIRenderFeatureModule_v1_Name) != nullptr` is still
   **2** (unchanged — the 2 new plugins answer `_v2`'s own capability
   string, never `_v1`'s).

### Step 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Write a top-level summary (mirrors `editor-core-separation-3`/`-5`'s own
`CAMPAIGN_COMPLETION_REPORT.md` precedent): what shipped, per phase, in one
paragraph each; every Locked Design Decision from `PHASE0_MASTER_STRATEGY.md`
restated alongside how it was actually realized in the real, shipped code;
the full Step 3.3 regression evidence; any real deviation from the original
plan and why.

### Verification

Step 3.3 IS this phase's own verification — there is no separate,
additional check beyond it.

### What this phase does NOT do

- Does not add any new plugin capability, ABI surface, or drawing
  operation — purely docs + regression + closeout.

### Completion

Write `PHASE8_COMPLETION_REPORT.md` AND `CAMPAIGN_COMPLETION_REPORT.md`,
then final `git_add` + `git_commit` for this whole campaign.
