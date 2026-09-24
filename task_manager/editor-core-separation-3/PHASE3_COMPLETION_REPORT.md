# PHASE3 COMPLETION REPORT — `IRenderFeatureModule_v1`: A Plugin Contributes a Real Render-Graph Pass

**Phase file**: `PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md`. **Parent**:
`PHASE0_MASTER_STRATEGY.md`. **Predecessors**:
`PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md`, `PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md`
(both read in full, along with their own `PHASEn_COMPLETION_REPORT.md`, before
starting). **Branch**: `feature/editor-core-separation` (confirmed via
`git_status` before starting — clean tree, correct branch — and unchanged
throughout).

## Summary — what was actually built

1. **`plugins/gte_plugin_abi/IRenderFeatureModule.h`** (new) — `IRenderFeatureModule_v1`
   + `kIRenderFeatureModule_v1_Name`, exactly as the phase file's Step 3.1
   specifies.
2. **`plugins/gte_plugin_abi/IPluginRenderPassBuilder.h`** (new) —
   `IPluginRenderPassBuilder::AddFullscreenClearPass()`, exactly as Step 3.2
   specifies.
3. **`src/Core/Plugins/PluginRenderPassBuilderAdapter.h/.cpp`** (new,
   `gte_core`) — `PluginRenderPassBuilderAdapter`, implementing
   `IPluginRenderPassBuilder` by forwarding into a real
   `rg::RenderGraphBuilder::AddRenderPass()` call, confirmed against the
   REAL, current 9-parameter overload (`name, kind, viewScope, category,
   setup, execute[, drawKind, renderPassEvent, tags]`) via `search_in_dir`/
   `read_line` before writing anything — matches the phase file's own
   already-correct parameter-order note exactly. **Materially corrected from
   the phase file's own literal Step 3.3 code sketch** — see Deviation #1
   below; the sketch's omission of an explicit `renderPassEvent` argument is
   a real, live-testing-confirmed bug, not a stylistic choice.
4. **New `"PluginRenderFeatures"` provider** in
   `Core::RegisterOffscreenRenderPipelineProviders()` (`src/Core/Core.cpp`) —
   registered as the LAST `Register(...)` call, immediately after
   `"AtmosphereComposite"`, with `rg::ProviderTiming::AfterDeferredPasses`,
   exactly as Step 3.4 specifies. Generically loops
   `m_pluginHost.AllLoadedModules()`, queries `IRenderFeatureModule_v1`, and
   calls `AddRenderGraphPasses()` on every module that implements it — zero
   hardcoded knowledge of `DemoRenderFeaturePlugin` anywhere in this
   function. **Materially corrected from the phase file's own literal Step
   3.4 code sketch in two further ways** — see Deviations #2/#3 below.
5. **`plugins/demo_render_feature/`** (new) — `CMakeLists.txt` (mirrors
   `plugins/demo_hello_world/CMakeLists.txt`'s own shape exactly, including
   its `PREFIX ""` fix and `gte_apply_plugin_shared_crt_linkage()` call) +
   `RenderFeaturePlugin.cpp` (implements `IRenderFeatureModule_v1`, contributes
   one `AddFullscreenClearPass("DemoRenderFeaturePlugin_Clear", 1, 0, 1, 1)`
   call — solid magenta), staged into the same `GTE_PLUGIN_RUNTIME_OUTPUT_DIR`
   `demo_hello_world.dll` already uses.
6. **Root `CMakeLists.txt`**:
   - `src/Core/Plugins/PluginRenderPassBuilderAdapter.h/.cpp` added to
     `gte_core`'s `target_sources()`, immediately after
     `src/Core/Plugins/PluginHost.cpp` (confirmed real, current line via
     `search_in_dir` first — was line 303).
   - `add_subdirectory(plugins/demo_render_feature)`, added immediately after
     the existing `add_subdirectory(plugins/demo_hello_world)` line inside the
     same `if(GTE_ENABLE_PLUGINS)` block (confirmed real, current line via
     `search_in_dir` first — was line 718).

## Step 4 verification (mandated by the phase file) — final, passing state

1. **Incremental compile, in order**: `demo_render_feature` → `gte_core` →
   `gte_editor` (no work to do — untouched) → `GreatTamanaEditor`. All built
   cleanly with zero warnings from this phase's own new code. `demo_render_feature.dll`
   confirmed to genuinely exist in `build/plugins/` via `browse_dir`.
2. **Live runtime smoke test**: `run_app_background` →
   `GET /activate_tab?name=Game` → `GET /get_game_view` — **the returned image
   is genuinely, visibly solid magenta** (confirmed via direct visual
   inspection of the returned PNG, not just HTTP 200).
3. **`GET /get_logs?limit=20`** — confirmed both `HelloWorldPlugin` and
   `DemoRenderFeaturePlugin` logged as loaded (`PluginHost` category, `Info`
   level), and `GET /get_logs?limit=30&min_level=warning` returned zero
   warnings/errors.
4. **Scene View also shows the same magenta clear**: `GET /activate_tab?name=Scene`
   → `GET /get_texture?texture_name=SceneViewComposited` — solid magenta,
   proving `ProviderScope::PerActiveView` genuinely ran this pass for both
   views.
5. **`GET /get_swapchain`** — visually confirmed the rest of the Editor UI
   (Hierarchy/Inspector/Project/Render Graph panels, dock layout) renders
   correctly with no regression, and the "Render Graph" panel's own pass list
   shows `"DemoRenderFeatureP..."` scheduled as the LAST pass, writing
   `SceneViewComposited`/`GameViewComposited` — the structurally correct
   position.
6. `stop_app_background` — done after each of the three separate live runs
   this phase needed (see Deviations below for why three, not one).

## Deviations from the strategy doc (real, discovered, not silently smoothed over)

This phase needed **three** real, live-testing-confirmed corrections beyond
the phase file's own literal code sketches — none of these were "wrong
line number" drift (Universal Rule 9's most common case); all three are
genuine logic bugs in the strategy doc's own necessarily-approximate
sketch, only discoverable by actually running the engine and looking at the
resulting image, exactly as this phase's own Step 4 (and PHASE0's Universal
Rule 10) require. The first magenta smoke test, run immediately after a
clean, warning-free compile of every new file exactly as the phase file
specified, showed **no magenta at all** — a plain sky gradient, byte-for-byte
what a build with no plugin at all would show. Diagnosing this required three
separate root-cause investigations and three separate rebuild+re-run cycles,
described below in the order they were found.

### Deviation #1 — `PluginRenderPassBuilderAdapter::AddFullscreenClearPass()` must tag `RenderPassEvent::AfterEverything` explicitly

The phase file's own Step 3.3 code sketch calls
`m_builder.AddRenderPass(debugName, kind, viewScope, category, setup, execute)`
— the 6-argument overload, leaving the trailing, defaulted `renderPassEvent`
parameter at its default, `RenderPassEvent::Opaques`.

Live testing (first `GET /get_game_view` smoke test) showed no magenta.
Investigating via the Editor's own "Render Graph" panel (a live
`GET /get_swapchain` screenshot, requested from the user mid-investigation to
confirm a hypothesis) showed `"DemoRenderFeaturePlugin_Clear"` scheduled
**between `"RenderOpaque"` and `"DrawSkyBackground"`** — i.e. immediately
overwritten by every real production pass that runs afterward.

Root cause, confirmed by re-reading `RenderGraphTypes.h`'s own
`RenderPassEvent` doc comment (render-pass-4 campaign, PHASE2):
`RenderGraphCompiler::Compile()`'s RAW/WAW dependency-edge scan walks passes
in **effective order** — `(RenderPassEvent, original declaration index)` —
not raw declaration order. A write-only pass (no `ReadTexture()` of its own)
has no genuine data dependency forcing it after later passes; its only
ordering signal is its own `RenderPassEvent` tier. Left at the default
`Opaques` tier (the same tier `"RenderOpaque"` uses), it is treated as an
EARLY writer relative to `"DrawSkyBackground"`/`"RenderTransparent"`/
`"AtmosphereComposite"` (all later tiers), so those passes are correctly
scheduled — and therefore execute — strictly AFTER it, overwriting the clear
before it is ever visible. `ProviderTiming::AfterDeferredPasses` (Step 3.4)
only controls WHEN the C++ `AddRenderPass()` call itself happens; it has no
effect on which `RenderPassEvent` tier the resulting `PassRecord` is
scheduled into.

**Fix**: `PluginRenderPassBuilderAdapter::AddFullscreenClearPass()` now passes
`rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything`
explicitly as the two final, previously-omitted trailing arguments —
`AfterEverything` (value `9000`) is the latest tier `RenderGraphTypes.h`
defines, the correct match for "draws on top of literally everything else
this frame."

### Deviation #2 — a correctly-ordered write-only pass into a never-read handle is still silently culled

After Deviation #1's fix, a second live smoke test (`GET /get_game_view` AND
`GET /get_texture?texture_name=GameView`, the raw pre-composite handle) still
showed no magenta, and `"DemoRenderFeaturePlugin_Clear"` had disappeared from
the "Render Graph" panel's pass list entirely.

Root cause, confirmed by re-reading `RenderGraphCompiler.cpp`'s own Step 2
comment (backward-reachability-from-`finalOutputs` culling): a pass is only
ever kept if it (directly, by writing a handle already inside
`finalOutputs`, or transitively, by being a predecessor of a kept pass)
contributes to a final output. `viewData->colorTarget` (`"GameView"`/
`"SceneView"`) is never itself pushed into `RenderPassFrameContext::finalTextureOutputs`
by any existing provider — only `"GameViewComposited"`/`"SceneViewComposited"`
and the various LUT handles are. Once the plugin's clear pass became this
handle's chronologically LAST writer (Deviation #1's own fix), nothing
downstream ever reads it again, so the compiler correctly (if unhelpfully)
discarded the pass as dead code — it was silently culled, never even
recorded.

**Fix**: the `"PluginRenderFeatures"` provider now pushes whatever handle it
actually targeted into `frame.finalTextureOutputs` itself, whenever at least
one plugin capability actually ran this frame — the exact same mechanism
`"AtmosphereSharedLut"`/`"AtmosphereViewLut"`/`"AtmosphereComposite"` already
use, and precisely what `RenderPassFrameContext::finalTextureOutputs`'s own
doc comment (`RenderPipeline.h`) already documents as the intended pattern:
"any PROVIDER that owns a handle needing this treatment simply appends it
here directly."

### Deviation #3 (the significant one — resolved via `ask_questions`) — the plugin must draw on the POST-composite image, not the raw pre-composite one

After Deviations #1 and #2's fixes, a third live smoke test showed
`GET /get_texture?texture_name=GameView` (the raw handle) correctly, finally,
solid magenta — proving the pass genuinely executes, in the right place, no
longer culled. **But `GET /get_game_view` itself still showed no magenta at
all.**

This is where the issue stopped being a simple "wrong parameter" mistake and
became a genuine architectural question the strategy doc never addresses:
`viewData->colorTarget` (`"GameView"`) is the RAW, PRE-atmosphere-composite
render target. `"AtmosphereComposite"` (registered immediately before
`"PluginRenderFeatures"`, same `AfterDeferredPasses` phase) already reads
`"GameView"` and produces a genuinely SEPARATE texture,
`"GameViewComposited"` — and per `src/Editor/FrameDebuggerHistory.h`'s own
doc comment on `compositedPreview`, **`"GameViewComposited"` is the TRUE
final image the "Game" panel / `GET /get_game_view` actually display**.
Nothing re-composites after `"AtmosphereComposite"` runs, so a plugin pass
that only ever writes the raw `"GameView"` handle is structurally invisible
in the actually-displayed output, no matter how correctly it is ordered
relative to other passes — a real, load-bearing gap the phase file's own
Step 3.4 sketch does not anticipate at all (it assumes `viewData->colorTarget`
IS the final displayed image).

Per this campaign's own Universal Rule 7 ("do not guess silently on anything
architecturally significant"), this was raised via `ask_questions` rather
than resolved unilaterally, offering two real options: (a) retarget the
plugin's pass onto the post-composite texture via a new blackboard hand-off,
matching the phase file's own literal Step 4 instruction to verify via
`GET /get_game_view`; or (b) keep targeting the raw pre-composite texture and
instead correct Step 4's own verification method to use
`GET /get_texture?texture_name=GameView`. **Answer: option (a).**

**Fix, implemented exactly per that answer**: two new
`RenderPassBlackboard` keys, `kGameCompositedOutputKey`/
`kSceneCompositedOutputKey` (mirroring `kAtmosphereViewLutGameKey`/
`kAtmosphereViewLutSceneKey`'s own existing per-view-key precedent
immediately above them in `Core.cpp`). The `"AtmosphereComposite"` provider
now `Publish()`es its own freshly-computed `composited` handle under the
matching per-view key, immediately after its existing
`frame.finalTextureOutputs.push_back(composited);` call. `"PluginRenderFeatures"`
now `Fetch()`es this handle back
(`.value_or(viewData->colorTarget)` — a defensive fallback to the raw handle
for the should-never-normally-happen case where `"AtmosphereComposite"`
returned early this frame without publishing anything) and constructs
`PluginRenderPassBuilderAdapter` against THAT handle instead of
`viewData->colorTarget` directly. Since `"GameViewComposited"`/
`"SceneViewComposited"` is already inside `frame.finalTextureOutputs`
(pushed by `"AtmosphereComposite"` itself, moments earlier in the same
provider-invocation loop), the plugin's own write to that same handle is
correctly recognized as writing a root and is never culled; Deviation #2's
own defensive `finalTextureOutputs.push_back()` in `"PluginRenderFeatures"`
was kept as a harmless, always-correct safety net (a no-op duplicate in the
common case, a genuine fix in the rare fallback case).

**Re-verified end-to-end after all three fixes together**: `GET /get_game_view`
returns solid magenta; `GET /get_texture?texture_name=SceneViewComposited`
(Scene View active) also returns solid magenta; the "Render Graph" panel now
shows `"DemoRenderFeatureP..."` as the genuinely last pass in the list,
writing `SceneViewComposited`/`GameViewComposited` directly — the
structurally correct position matching this phase's own stated design
intent ("draws on top of whatever the real frame already drew").

## Files added/changed

- `plugins/gte_plugin_abi/IRenderFeatureModule.h` (new)
- `plugins/gte_plugin_abi/IPluginRenderPassBuilder.h` (new)
- `src/Core/Plugins/PluginRenderPassBuilderAdapter.h` (new)
- `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp` (new)
- `plugins/demo_render_feature/CMakeLists.txt` (new)
- `plugins/demo_render_feature/RenderFeaturePlugin.cpp` (new)
- `src/Core/Core.cpp` (modified — new includes; new
  `kGameCompositedOutputKey`/`kSceneCompositedOutputKey` blackboard keys;
  `"AtmosphereComposite"` provider now publishes its composited handle;
  new `"PluginRenderFeatures"` provider)
- `CMakeLists.txt` (modified — `gte_core` `target_sources()` addition,
  `add_subdirectory(plugins/demo_render_feature)`)

## Non-Goals honored (per the phase file's own list)

No editor-tier capability was touched (`IEditorPanelModule_v1` is PHASE4's
job). `IPluginRenderPassBuilder` stays deliberately narrow — exactly the one
`AddFullscreenClearPass()` method the phase file specifies; no textured
draws/compute dispatches/cross-pass reads were added ahead of a real future
need.

## Compile-check summary (for the record)

- `demo_render_feature`: built cleanly (2 Ninja steps).
- `gte_core`: built cleanly (2 rebuilds across this phase's own investigation
  — `PluginRenderPassBuilderAdapter.cpp`/`Core.cpp` — both clean, zero
  warnings).
- `gte_editor`: "no work to do" every time (untouched by this phase).
- `GreatTamanaEditor`: linked cleanly every time; three separate live
  `run_app_background` + `gte_send_request` smoke-test round-trips performed
  during this phase's own investigation (one per deviation found above), the
  final one fully passing every one of Step 4's five checklist items.
