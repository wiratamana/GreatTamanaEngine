# PHASE8 — Docs, Full Regression, Campaign Closeout — COMPLETION REPORT

**Status: DONE.** Implemented per
`PHASE8_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`'s own Step 3.1–3.4, with
one confirmed, necessary, additive fix found during this phase's own mandatory
probe re-run (see "Deviations from the plan" below).

## What changed

### 1. `docs/conventions/plugin-architecture.md`

- The pre-existing "Multiple `IRenderFeatureModule_v1` plugins" paragraph was
  corrected in place — it now explicitly reads as "the LEGACY, still-supported
  `_v1` path" and points at the new `_v2` section immediately below for the
  real fix, instead of ending on "per-plugin compositing is explicitly
  deferred, not yet designed" (which is no longer true — it now exists, as
  `_v2`). No sentence describing `_v1`'s own real, unchanged, shared-target
  "last write wins" behavior was altered — only the closing sentence
  describing the state of a FIX was corrected.
- New section, **"`_v2` Render-Feature System — Real Multi-Plugin
  Compositing"** — covers `IRenderFeatureModule_v2`,
  `GtePluginRenderFeatureDescriptor` (`stage`/`priority`/`blendMode`),
  `IPluginRenderPassBuilder_v2`'s 3 fixed operations, the per-plugin-private-
  target + host-owned GPU blend compositing model, explicitly states which
  `RenderFeatureStage` values are wired (`PostComposite`/`PreUI`) vs.
  declared-but-refused (`PreOpaque`/`PostOpaque`/`PostTransparent`) and why
  (PHASE0 Locked Design Decision #1), the `PostComposite`→`PreUI` two-sub-stage
  ordering rationale (Locked Design Decision #2), and the explicitly
  unspecified `_v1`-vs-`_v2` coexistence outcome (Locked Design Decision #9).
- New section, **"`IPluginCapabilityOrchestrator` — Generic Plugin Capability
  Registry"** — documents the interface, `Core::RegisterBuiltinCapabilityOrchestrators()`'s
  vector-of-orchestrators mechanism, and lists all 3 built-in implementations
  (`LegacyRenderFeatureOrchestrator`, `EditorPanelCapabilityOrchestrator`,
  `RenderFeatureCompositor`), stating plainly that a future capability kind
  should add a FOURTH orchestrator here rather than hand-editing
  `Core::LoadPlugins()`/`Core::RegisterOffscreenRenderPipelineProviders()`
  again.
- Both new sections inserted immediately after the corrected `_v1` paragraph,
  before the pre-existing "Failure-path regression coverage" section — no
  other paragraph in the file was touched.

### 2. `AGENTS.md`

**Confirmed via `search_in_dir` for "last write wins" across the whole repo:
`AGENTS.md` itself has ZERO hits.** Its own "Plugin Architecture" section
describes the ABI foundation (fingerprint gate, curated-interface boundary
rule, shared-CRT caveat) and never claims anything about render-feature
compositing being unsupported — so this campaign's change does not make any
existing sentence in it more or less true. Per the phase file's own
instruction ("confirm via `search_in_dir` whether `AGENTS.md` actually mentions
this at all before assuming an edit is needed"), **no edit was made.**

### 3. `plugins/gte_plugin_abi/PublicSurface.md`

Re-read `RenderFeatureDescriptor.h`, `IPluginRenderPassBuilder_v2.h`, and
`IRenderFeatureModule.h`'s `_v2` addition in their FINAL, real, shipped shape
(post-PHASE4/5/6). **Confirmed the PHASE1 bullet is still byte-accurate** —
`GtePluginRenderFeatureDescriptor`'s fields (`name`/`stage`/`priority`/
`blendMode`), `IPluginRenderPassBuilder_v2`'s 3 methods, and
`IRenderFeatureModule_v2`'s 2 methods never drifted from PHASE1's own initial
sketch during PHASES 4–6's real implementation work. Appended a short
"Re-confirmed accurate as of PHASE8" note to the existing PHASE1 bullet
(rather than rewriting it, since nothing needed correcting) plus an explicit
statement that `IPluginCapabilityOrchestrator`/`LegacyRenderFeatureOrchestrator`/
`EditorPanelCapabilityOrchestrator`/`RenderFeatureCompositor`/
`PluginRenderPassBuilderAdapter_v2` all live under `src/Core/Plugins/`
(`gte_core`-internal) and never crossed into `plugins/gte_plugin_abi/` at any
point in PHASES 2–7 — confirmed by direct re-read of every new file those
phases added, per the phase file's own Step 2 instruction.

### 4. `tools/ci/gte_plugin_isolation_probe/main.cpp`/`README.md`/`CMakeLists.txt`

Updated per Step 3.3 item 4 — the probe's own hardcoded module-count
expectation moved from **4** to **6** (`host.LoadedModuleCount() != 6`), with
updated doc comments in all 3 files explaining the PHASE6 `_v2` demo-plugin
addition. The `IRenderFeatureModule_v1` implementer count assertion stays at
**2** (unchanged — the 2 new `_v2` plugins answer `IRenderFeatureModule_v2`'s
own distinct capability string, never `_v1`'s, so this count is correctly
unaffected).

### 5. `CMakeLists.txt` — real bug found and fixed (see "Deviations" below)

`add_dependencies(gte_plugin_isolation_probe demo_hello_world
demo_render_feature demo_editor_panel demo_render_feature_second
demo_render_feature_v2 demo_render_feature_v2_second)` — the 2 new `_v2`
plugin names were added to this list (previously only listed the original 4).

## Deviations from the plan

**One confirmed, necessary, additive fix — found by this phase's own mandatory
Step 3.3 item 4 probe re-run itself, not anticipated by the phase file's own
text:**

Re-running `tools/ci/gte_plugin_isolation_probe` after updating `main.cpp`'s
own expected count to 6 (per the phase file's own instruction) initially
**FAILED** — the probe's own inner build only ever rebuilt
`demo_render_feature`/`demo_render_feature_second` (never
`demo_render_feature_v2`/`demo_render_feature_v2_second`), and inspecting the
inner build's own `plugins/` output folder confirmed the two new `_v2` `.dll`s
were never even compiled, let alone copied there. **Root cause, confirmed by
direct read of the root `CMakeLists.txt`:** `gte_plugin_isolation_probe`'s own
`add_dependencies(...)` call (line ~1207, pre-existing since
`editor-core-separation-4` PHASE5) explicitly names only the ORIGINAL 4 demo
plugins — `cmake --build <inner-dir> --target gte_plugin_isolation_probe`
(the OUTER wrapper project's own exact invocation, `tools/ci/gte_plugin_isolation_probe/CMakeLists.txt`)
only builds that one target's own dependency closure, never every CMake target
in the configure — so PHASE6's 2 new plugin subdirectories, though correctly
`add_subdirectory()`'d (inherited automatically from the shared root
`CMakeLists.txt`, since the inner configure is the SAME root project with
`GTE_CORE_STANDALONE_PROBE_ONLY=ON`), were simply never built by this specific
probe's own build command. **Fix:** added `demo_render_feature_v2` and
`demo_render_feature_v2_second` to that same `add_dependencies(...)` call.
Re-ran the probe build afterward — the two new plugin `.dll`s compiled and
linked correctly this time, and the probe then printed
`PASS: 6 plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1, ...`.

This is exactly the kind of "a real, newly-broken, unexplained... needs a
dedicated fix" situation Workflow Rule 7 describes — but it was found and
fixed directly within this same implementation session (not via
`delegate_task`, since the root cause was immediately clear and the fix was
small, safe, and additive), and it is not a `ctest` failure (this whole
campaign adds no new automated `ctest` per Locked Design Decision #7) — it is
a pre-existing gap in a MANUALLY-invocable CI probe's own `CMakeLists.txt`
dependency list, exactly the kind of thing this phase's own Step 3.3 item 4
explicitly assigned to Phase8 to confirm and fix. No other file's behavior
changed as a result of this fix — `gte_plugin_abi_handshake_probe` (which
never references plugin counts at all) needed no equivalent change, confirmed
by re-reading its own `main.cpp` before assuming so.

No other deviation. Every docs update matches the phase file's own Step
3.1–3.2 instructions exactly.

## Verification evidence (Step 3.3 — this phase's own required regression pass)

### 1. Full build

`cmake --build build` (existing, already-configured `build/` tree, Ninja/
MinGW) — **`ninja: no work to do`**, confirming every target was already
up to date from PHASES 1–7's own incremental builds (this phase's own changes
are docs/CI-probe-only, touching zero `src/`/`plugins/` production code).

### 2. Full `ctest` regression pass

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
Debug --output-on-failure`:

**1787 total tests, 100% of executed tests passed, 2 legitimate,
environment-gated skips, zero failures**:

- `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
  (pre-existing, gated on a real MMD model file not present on this machine).
- `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
  (pre-existing, this machine's Vulkan driver lacks `VK_EXT_headless_surface`).

**Before/after comparison against the most recent prior campaign's own
baseline** (`editor-core-separation-5/CAMPAIGN_COMPLETION_REPORT.md`: "1787
total, 1785 passed, 2 legitimate skips, zero failures"): **this run is exactly
the same — 1787 total, 1785 passed, 2 legitimate skips, zero failures. Zero
test-count drift, in either direction**, across the ENTIRE 8-phase
`editor-core-separation-6` campaign — the expected result, since this whole
campaign never adds a new automated `ctest` for real rendered-pixel content
(Locked Design Decision #7) and never touched any existing `TEST()`/`TEST_F()`
case.

### 3. Live, HTTP-driven smoke test

Ran `build\GreatTamanaEditor.exe` via `run_app_background` (PID 1408):

**`GET /get_logs?limit=200`** (no prior `/clear_logs` — the plugin-load-time
warning fires before any HTTP client could possibly connect and clear it,
exactly like every prior phase's own recorded evidence) — all 6 demo plugins
loaded correctly (`DemoEditorPanelPlugin`, `HelloWorldPlugin`,
`DemoRenderFeaturePlugin`, `DemoRenderFeaturePluginSecond`,
`DemoRenderFeatureV2Plugin`, `DemoRenderFeatureV2SecondPlugin`), with the exact
`_v1` multi-plugin warning text, byte-for-byte unchanged since PHASE2's own
recorded baseline:

```
"2 loaded plugins implement IRenderFeatureModule_v1 - only the LAST-registered
one's render output will be visible this frame (render-graph compositing for
multiple render-feature plugins is not implemented - see
docs/conventions/plugin-architecture.md)."
```

(3 pre-existing, unrelated `RenderGraph` GPU-timing-slot-budget-exhaustion
warnings also present, same 3 pass names PHASE6/7 already documented — no new
warning of any kind.)

**`GET /list_tabs`** — returned
`["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Log","Project","Demo Plugin Panel"]`
— byte-for-byte identical to every prior phase's own recorded baseline (PHASE3
onward).

**`GET /get_game_view`** (with the FINAL, permanent `plugins/` folder state —
all 4 original demo plugins + the 2 permanent PHASE6 `_v2` demo plugins) —
real screenshot confirmed via `load_image`: solid BLUE center, solid RED
edges/corners, a soft red-to-blue gradient ring in between — the EXACT same
`_v2`-wins-over-`_v1`-magenta composited result PHASE6/PHASE7's own completion
reports already documented and explained (Locked Design Decision #9 — an
explicitly non-deterministic, accepted interaction, still stable across
PHASES 2–8 since `LegacyRenderFeatureOrchestrator`'s and
`RenderFeatureCompositor`'s relative registration order inside
`Core::RegisterBuiltinCapabilityOrchestrators()` never changed) — confirming
NO regression was introduced by PHASES 7–8's own edits.

**`GET /activate_tab?name=Render Graph`** → `{"activated_tab":"Render Graph","success":true}`,
followed by **`GET /get_swapchain`** — real screenshot confirms the "Render
Graph" panel's "Plugin Render Features" section still shows, character-for-
character identical to PHASE7's own recorded baseline:

```
Plugin Render Features
[PostComposite] DemoRenderFeatureV2 - priority 0, blend Replace
[PreUI] DemoRenderFeatureV2Second - priority 0, blend AlphaOver
```

Both the "Scene" and "Game" panels show the identical RED/BLUE composited
result simultaneously.

`stop_app_background` called at the end of the check (PID 1408).

### 4. Both standalone CI probes, re-run fresh

- **`tools/ci/gte_plugin_abi_handshake_probe`** — rebuilt
  (`cmake --build build-plugin-abi-handshake-probe`, `ninja: no work to do` —
  unaffected by this campaign), ran the `.exe` directly:
  `"OK: loaded plugin 'HelloWorldPlugin' v1.0.0 - Milestone 0 handshake proof -
  implements zero capabilities."`, exit code `0` — unaffected by this whole
  campaign (this probe never references plugin counts or render-feature
  capabilities at all).
- **`tools/ci/gte_plugin_isolation_probe`** — rebuilt after the
  `add_dependencies(...)` fix above; ran the `.exe` directly:

  ```
  Loaded 'DemoEditorPanelPlugin' - IRenderFeatureModule_v1: no
  Loaded 'HelloWorldPlugin' - IRenderFeatureModule_v1: no
  Loaded 'DemoRenderFeaturePlugin' - IRenderFeatureModule_v1: yes
  Loaded 'DemoRenderFeaturePluginSecond' - IRenderFeatureModule_v1: yes
  Loaded 'DemoRenderFeatureV2Plugin' - IRenderFeatureModule_v1: no
  Loaded 'DemoRenderFeatureV2SecondPlugin' - IRenderFeatureModule_v1: no
  PASS: 6 plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1, and
  this file never once asked any plugin for IEditorPanelModule_v1.
  ```

  exit code `0` — confirms `LoadedModuleCount()` now correctly reads **6** (4
  original + 2 new PHASE6 `_v2` plugins), and the count of modules answering
  `QueryCapability(kIRenderFeatureModule_v1_Name) != nullptr` is still **2**
  (unchanged — the 2 new plugins answer `_v2`'s own distinct capability
  string, confirmed by the explicit `"IRenderFeatureModule_v1: no"` printed
  for both `DemoRenderFeatureV2Plugin`/`DemoRenderFeatureV2SecondPlugin`).

### 5. `git_status`

Before commit, the diff is exactly:

```
modified:   CMakeLists.txt
modified:   docs/conventions/plugin-architecture.md
modified:   plugins/gte_plugin_abi/PublicSurface.md
modified:   tools/ci/gte_plugin_isolation_probe/CMakeLists.txt
modified:   tools/ci/gte_plugin_isolation_probe/README.md
modified:   tools/ci/gte_plugin_isolation_probe/main.cpp
```

— matching the phase file's own scope exactly (docs + `PublicSurface.md` +
the probe fix Step 3.3 item 4 explicitly assigned here), plus this report and
`CAMPAIGN_COMPLETION_REPORT.md`.

## What this phase does NOT do (confirmed honored)

- Did not add any new plugin capability, ABI surface, or drawing operation —
  purely docs + regression + a pre-existing CI-probe dependency-list bugfix +
  closeout.
- Did not touch any `_v1`-era or `_v2`-era production `src/`/`plugins/` code —
  confirmed via `git_status` (only `CMakeLists.txt`'s `add_dependencies(...)`
  line changed, a build-graph-only edit with zero compiled-code change).
- Did not add any new automated `ctest` for real rendered-pixel content
  (Locked Design Decision #7 — unchanged, zero new `TEST()`/`TEST_F()` case
  anywhere in this whole campaign).

## Summary

`editor-core-separation-6`'s docs now accurately describe the real, final
shipped `_v2` render-feature system and the generic
`IPluginCapabilityOrchestrator` registry, contrasted honestly against `_v1`'s
still-real, still-supported "last write wins" limitation. The one full clean
build + full `ctest` pass + live HTTP smoke test this whole campaign is
allowed to run all confirm **zero regression** relative to every phase's own
prior baseline — same test count (1787, zero drift), same log/warning text,
same panel names, same composited pixel result, same "Plugin Render Features"
panel text. A real, previously-latent bug in
`tools/ci/gte_plugin_isolation_probe`'s own `add_dependencies(...)` list (which
would have permanently prevented this probe from ever exercising the 2 new
`_v2` demo plugins) was found and fixed as part of this phase's own mandated
Step 3.3 item 4 re-verification. `CAMPAIGN_COMPLETION_REPORT.md` closes out the
whole 8-phase campaign.
