# editor-core-separation-6 — CAMPAIGN COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. This report is the single, top-level
summary of the whole 8-phase campaign, mirroring
`task_manager/editor-core-separation-5/CAMPAIGN_COMPLETION_REPORT.md`'s own
shape and tone. See each `PHASEn_COMPLETION_REPORT.md` in this same folder for
full per-phase detail.

## What this campaign set out to do

`GreatTamanaEngin-Ideas/render-feature/RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md`
found, and this campaign's own `PHASE0_MASTER_STRATEGY.md` re-confirmed via
direct source inspection, a real, proven bug: `IRenderFeatureModule_v1`'s
entire drawing API is one hard clear, and every loaded `_v1` render-feature
plugin is handed the EXACT SAME shared render target — with 2+ such plugins
loaded, only the LAST-registered one's output survives, silently, "last write
wins," chosen by filesystem directory-scan order. This campaign's goal was to
ship a real, additive, strictly-alongside-`_v1` `_v2` render-feature system
that fixes this for real: per-plugin private offscreen targets, a real
host-owned GPU blend compositor (5 blend modes), author-declared stage +
priority ordering, a small growable palette of host-implemented drawing
operations, a generic `IPluginCapabilityOrchestrator` registry proving the
pattern generalizes beyond render features, and a real, live, pixel-verified
proof that 2 simultaneously-loaded `_v2` plugins genuinely composite
correctly — closing the Proposal's own documented embarrassment where the
original 2 `_v1` demo plugins deliberately cleared to the identical color,
hiding the bug from every prior screenshot-based smoke test.

## What shipped, phase by phase

**PHASE1 — Render Feature ABI v2 Foundation.** New, additive-only ABI headers
under `plugins/gte_plugin_abi/`: `RenderFeatureDescriptor.h`
(`RenderFeatureStage`, `RenderFeatureBlendMode`,
`GtePluginRenderFeatureDescriptor`, `MakeRenderFeatureDescriptor()`),
`IPluginRenderPassBuilder_v2.h` (exactly 3 fixed drawing operations —
`AddSolidFillPass`/`AddRadialVignettePass`/`AddColorGradePass`), and
`IRenderFeatureModule_v2` appended (never replacing) the existing
`IRenderFeatureModule.h`. Zero `gte_core` change; verified by a standalone
compile check limited to exactly `plugins/gte_plugin_abi`'s own include path.

**PHASE2 — `IPluginCapabilityOrchestrator` Registry + Legacy Render-Feature
Migration.** The new, generic `IPluginCapabilityOrchestrator` interface
(`OnPluginsLoaded()`/`ContributeRenderGraphPasses()`), `Core`'s new
`m_capabilityOrchestrators` vector, and migration of the EXISTING `_v1`
render-feature loop (multi-plugin warning + shared-target rendering) into a
new `LegacyRenderFeatureOrchestrator` — with **zero observable behavior
change** (same warning text, same magenta baseline, same pass names). One
necessary, additive fix found during this phase's own build: `Core` needed an
explicit, out-of-line destructor (the classic incomplete-forward-declared-type-
behind-`unique_ptr` pitfall).

**PHASE3 — Editor Panel Orchestrator Migration.** `EditorHost.cpp`'s
constructor reordered (the 10/11 `RegisterBuiltinPanelName(...)` calls now run
BEFORE `Core::LoadPlugins()`, not after — Locked Design Decision #6), then
migration of the existing `IEditorPanelModule_v1` wiring into a new
`EditorPanelCapabilityOrchestrator` — again **zero observable behavior
change**, verified via `GET /list_tabs` showing every built-in panel name
first, `"Demo Plugin Panel"` still last, exactly as before.

**PHASE4 — `RenderFeatureCompositor` Core: Ordering, Private Targets,
Collision Detection.** The real `PluginRenderPassBuilderAdapter_v2`
(implementing the 3 fixed drawing operations via the new
`RenderFeatureOps.comp` uber compute shader) and `RenderFeatureCompositor`
itself (a third `IPluginCapabilityOrchestrator`): descriptor collection at
`OnPluginsLoaded()` time, per-stage priority sorting with a documented,
stable, lexical tie-break, loud collision-detection warnings, per-plugin
private offscreen targets, and a per-view SEED dispatch (copying the view's
current composited image into a private seed target, closing a same-physical-
image read+write hazard the plan's own pseudocode had not fully spelled out) —
with blending TEMPORARILY hardcoded to a Replace-equivalent stub to prove the
whole ordering/private-target pipeline with exactly one loaded `_v2` demo
plugin (verified: solid GREEN via a throwaway probe plugin, cleaned up
afterward) before the real multi-mode blend shader existed.

**PHASE5 — Real Blend-Mode Compute Shader + `PostComposite`→`PreUI` Sub-Stage
Ordering.** The real, permanent `RenderFeatureBlend.comp` uber blend shader (5
modes — `Replace`/`AlphaOver`/`Additive`/`Multiply`/`ScreenSpaceMask` — one
push-constant integer selects the formula), replacing PHASE4's throwaway
Replace-only stub outright (deleted), plus wiring the second, back-to-back
`PreUI` sub-stage (Locked Design Decision #2). Verified with 2 differently-
configured, differently-blended, differently-staged throwaway plugins: a
solid-blue vignette center over a solid-red fill, exactly matching the
hand-computed expected `AlphaOver` blend result pixel-for-pixel — real,
positive proof the ordering AND the blend math are both genuinely correct, not
merely "looks about right."

**PHASE6 — Permanent `_v2` Demo Plugins + Real Pixel-Level Compositing Proof.**
Two new, PERMANENT, committed demo plugins —
`plugins/demo_render_feature_v2/` (`PostComposite`, `Replace`, opaque RED
fill) and `plugins/demo_render_feature_v2_second/` (`PreUI`, `AlphaOver`, a
BLUE radial vignette) — genuinely different colors AND spatial patterns
(never the same-color trick `_v1`'s own demos used). A real, live, HTTP-driven,
MATHEMATICALLY-VERIFIED per-pixel comparison (a throwaway, never-committed PNG
decoder script) confirmed 3 sampled pixels (center/corner/gradient-ring) match
the hand-computed expected `AlphaOver` blend formula exactly or within a single
8-bit quantization step — closing the Proposal's own documented embarrassment
for real, with real evidence, not log text.

**PHASE7 — "Plugin Render Features" Section in the Editor's "Render Graph"
Panel.** A new `RenderFeatureDebugEntry` POD (in its own dependency-free
header, mirroring `GpuDrivenBatchDebugInfo.h`'s own established precedent —
a small, documented deviation from the phase file's own inline-header sketch),
`RenderFeatureCompositor::DebugSnapshot()` (a pure, read-only accessor), and a
new `BuildPluginRenderFeaturesSection()` in `RenderGraphPanel.cpp` — verified
via a real screenshot showing the compositor's own real, resolved ordering
decision (`"[PostComposite] DemoRenderFeatureV2 - priority 0, blend Replace"`,
`"[PreUI] DemoRenderFeatureV2Second - priority 0, blend AlphaOver"`),
character-for-character matching the phase's own expected text.

**PHASE8 (this phase) — Docs, Full Regression, Campaign Closeout.** Updated
`docs/conventions/plugin-architecture.md` (a new `_v2` section, a new
`IPluginCapabilityOrchestrator` section, and a correction to the pre-existing
`_v1` paragraph pointing at the real fix instead of calling it "not yet
designed"); confirmed `PublicSurface.md`'s PHASE1 bullet is still byte-accurate
against the final, real, shipped ABI shape (no drift found); confirmed
`AGENTS.md` needed no edit (never mentioned the `_v1` limitation by name); ran
the ONE full clean build + full `ctest` pass + live HTTP smoke test + both
standalone CI probes this whole campaign is allowed to run; found and fixed a
real, previously-latent bug in `tools/ci/gte_plugin_isolation_probe`'s own
`CMakeLists.txt` (`add_dependencies(...)` never listed the 2 new PHASE6 `_v2`
plugins, so this probe's own build command would never have built them); wrote
this report.

## Locked Design Decisions from `PHASE0_MASTER_STRATEGY.md` — restated alongside how they were actually realized

1. **Only `PostComposite`/`PreUI` wired; `PreOpaque`/`PostOpaque`/`PostTransparent`
   declared-but-refused.** Realized exactly as specified —
   `RenderFeatureCompositor::OnPluginsLoaded()` refuses any of the 3 unwired
   stages with a loud `GTE_LOG_WARNING`, never invokes that plugin afterward
   (PHASE1 declared the enum; PHASE4 implemented the refusal).
2. **`PostComposite` and `PreUI` are two ordered sub-stages at the SAME
   existing hook, not a new `RenderPassEvent` tier.** Realized exactly —
   `ContributeRenderGraphPasses()` processes the combined
   `PostComposite`-then-`PreUI` list in one pass, both still tagged
   `RenderPassEvent::AfterEverything` (PHASE4/PHASE5), proven correct via
   PHASE5's own 2-plugin pixel-level verification.
3. **No "read scene color/depth" capability this campaign.** Never added —
   `IPluginRenderPassBuilder_v2` has exactly 3 draw-only operations, confirmed
   by direct re-read of the final header (PHASE8's own `PublicSurface.md`
   re-verification).
4. **`IEditorPanelModule_v1` DOES migrate onto the new registry, as a second
   proof it generalizes.** Realized in PHASE3, with the required
   `EditorHost.cpp` reordering fix applied first (Locked Design Decision #6).
5. **All 5 blend modes as ONE uber compute shader; same philosophy for the 3
   fixed drawing operations.** Realized exactly — `RenderFeatureOps.comp`
   (PHASE4) and `RenderFeatureBlend.comp` (PHASE5), both single files, mode
   selected via a push-constant integer.
6. **`EditorHost.cpp` constructor reordered — built-in panel names register
   BEFORE `Core::LoadPlugins()`.** Realized in PHASE3, verified via `GET
   /list_tabs` showing every built-in name first, `"Demo Plugin Panel"` last,
   unchanged before/after.
7. **No new automated `ctest` for real rendered-pixel content.** Held for the
   entire campaign — PHASE6's pixel-level proof used a throwaway, never-
   committed Python PNG-decoding script against a LIVE running engine; PHASE8's
   own final `ctest` run confirms the total test count (1787) never changed
   across all 8 phases.
8. **Two brand-new plugin folders; all 4 existing demo plugins untouched.**
   Realized in PHASE6 (`demo_render_feature_v2`,
   `demo_render_feature_v2_second`); confirmed via `git_status` at every
   phase's own commit that none of `demo_render_feature`/
   `demo_render_feature_second`/`demo_editor_panel`/`demo_hello_world` was ever
   modified.
9. **`_v1` and `_v2` are NOT unified into one deterministic composited order —
   an accepted, explicitly out-of-scope edge case.** Confirmed, unchanged,
   through every phase's own live smoke test (PHASE4 onward): with both
   loaded, `_v2`'s own last blend write consistently won over `_v1`'s magenta
   in every single run across PHASES 4–8, because `RenderFeatureCompositor`'s
   registration always runs after `LegacyRenderFeatureOrchestrator`'s inside
   `Core::RegisterBuiltinCapabilityOrchestrators()` — a real, stable,
   documented (never "fixed") fact, not a flaky coincidence.
10. **The compositor's final blend write, for the last plugin in the last
    wired stage, writes directly into the SAME `pluginTarget` handle `_v1`
    already writes into.** Realized exactly in PHASE4/PHASE5 — confirmed by
    every phase's own `GET /get_game_view`/`GET /get_swapchain` check working
    with zero change anywhere else in the engine.

## Full regression evidence (PHASE8's own mandatory checkpoint, restated here)

- **Full build**: `cmake --build build` (existing, already-configured tree) —
  `ninja: no work to do` (everything already current from PHASES 1–7's own
  incremental builds).
- **Full `ctest -C Debug --output-on-failure`**: **1787 total tests, 100% of
  executed tests passed, 2 legitimate, pre-existing, environment-gated skips,
  zero failures** — byte-identical to `editor-core-separation-5`'s own final
  baseline. **Zero test-count drift across this entire 8-phase campaign** —
  the expected, correct result, since this campaign deliberately never adds a
  new automated `ctest` for rendered-pixel content (Locked Design Decision #7)
  and never touched an existing `TEST()`/`TEST_F()` case.
- **Live HTTP smoke test**: `GET /get_logs` shows the exact, byte-for-byte
  unchanged `_v1` multi-plugin warning text; `GET /list_tabs` is unchanged;
  `GET /get_game_view` shows the exact same RED/BLUE `_v2`-composited result
  PHASE6/7 already documented; the "Render Graph" panel's "Plugin Render
  Features" section still lists both permanent `_v2` demo plugins with the
  exact expected text.
- **Both standalone CI probes** — `gte_plugin_abi_handshake_probe` (unaffected,
  unchanged output) and `gte_plugin_isolation_probe` (fixed and re-verified —
  now correctly reports `LoadedModuleCount() == 6`, `IRenderFeatureModule_v1`
  implementer count still `2`).

## Deviations from the original plan (consolidated from every phase's own report)

1. **PHASE1** — one tiny, mechanical addition (`#include <cstddef>`), zero
   design deviation.
2. **PHASE2** — `Core` needed an explicit, out-of-line destructor (a real,
   necessary C++ correctness fix for a forward-declared type behind
   `unique_ptr`), found and fixed during this phase's own mandatory
   incremental-build verification.
3. **PHASE3** — no deviations.
4. **PHASE4** — two small, necessary, additive deviations while filling in
   real Vulkan-binding details the plan's own pseudocode left slightly
   underspecified: `Core::FindPluginRenderFeatureTarget()` became non-`const`
   and gained a `sampler` field (needed for the seed dispatch's `sampler2D`
   input), and each private-target/blend-stage `ComputeDescriptorSet` is
   allocated once at state-creation time rather than lazily inside
   `DispatchOps()` (a purely organizational refinement, identical runtime
   behavior).
5. **PHASE5** — no deviations.
6. **PHASE6** — no deviations.
7. **PHASE7** — two small, documented, precedent-following deviations:
   `RenderFeatureDebugEntry` placed in its own header (mirroring
   `GpuDrivenBatchDebugInfo.h`'s own established precedent) rather than inline
   inside `RenderFeatureCompositor.h`, and 4 files (not 1) needed a one-line
   signature change to thread the new argument through
   `IEditorLayer::BuildUI()`'s pure-virtual interface.
8. **PHASE8 (this phase)** — one confirmed, necessary, additive fix:
   `tools/ci/gte_plugin_isolation_probe`'s own `add_dependencies(...)` list in
   the root `CMakeLists.txt` never listed the 2 new PHASE6 `_v2` demo plugins,
   so this probe's own build command (`cmake --build <inner-dir> --target
   gte_plugin_isolation_probe`, which only builds that one target's own
   dependency closure) would have silently, permanently never built them —
   found by this phase's own mandatory Step 3.3 item 4 probe re-run, fixed by
   adding both plugin names to that same list.

## What remains genuinely open (honest, not silently dropped)

- **`PreOpaque`/`PostOpaque`/`PostTransparent` remain unwired** — declared in
  the ABI for future-proofing only, refused loudly at runtime if a plugin uses
  one. Wiring them means inserting new hook points into the LIVE opaque/
  transparent production render passes — explicitly deferred to a future
  follow-up campaign.
- **No "read the current scene color/depth" plugin capability** — none of the
  3 fixed drawing operations need it; a future `_v3` (or a purely additive
  extension to `_v2`) is the right place to add this once a real consumer
  (e.g. a depth-aware outline effect) needs it.
- **`_v1` and `_v2` render-feature ordering remain unspecified when both are
  loaded simultaneously** — an accepted, explicitly out-of-scope edge case
  (Locked Design Decision #9), never fixed, by design.
- **No new automated `ctest` for real rendered-pixel content** — this
  campaign's own pixel-level proof (PHASE6) remains a manual, live-engine,
  HTTP-driven procedure, matching this repo's own existing, documented
  "GPU-rendered pixels = no `ctest` coverage yet" convention.
- **The shared-runtime-capable toolchain switch remains a deliberately
  deferred, separate decision** — unaffected by this campaign, still open from
  every prior `editor-core-separation-*` campaign.
- **No real CI pipeline exists for this repository** (unchanged) — both probes
  in this campaign remain manually-invocable local CMake projects.

`editor-core-separation-6` is complete. The engine now has a real, additive
`_v2` render-feature system that gives every simultaneously-loaded
render-feature plugin its own private render target, composited in
author-declared order through a real, host-owned 5-mode GPU blend shader —
proven correct with live, mathematically-verified per-pixel evidence, not just
a screenshot that "looks about right" — and a generic
`IPluginCapabilityOrchestrator` registry that closes the door on Core needing a
bespoke hand-written loop for every future plugin capability kind. `_v1`
remains completely untouched, byte-for-byte, forever. Ready to merge.
