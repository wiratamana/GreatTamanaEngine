# PHASE6 — Permanent `_v2` Demo Plugins + Real Pixel-Level Compositing Proof — COMPLETION REPORT

**Status: DONE.** Implemented per `PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md`'s
own Step 3.1–3.4, with zero deviation from the plan.

## What changed

### 1. New plugin — `plugins/demo_render_feature_v2/`

`CMakeLists.txt` + `RenderFeaturePlugin.cpp`, mirroring
`plugins/demo_render_feature/CMakeLists.txt`'s exact shape (`add_library(...
SHARED)`, `target_link_libraries(... PRIVATE gte_plugin_abi)`,
`RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"`, `PREFIX ""`,
`gte_apply_plugin_dll_shared_crt_linkage(...)`). Implements
`IRenderFeatureModule_v2` via `SingleCapabilityPluginModule<T>` +
`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`, declaring:
- `stage = PostComposite`, `priority = 0`, `blendMode = Replace`
- `AddSolidFillPass("DemoRenderFeatureV2_Fill", 1.0, 0.0, 0.0, 1.0)` — an
  opaque RED solid fill.

Content matches the phase file's own Step 3.1 code block verbatim.

### 2. New plugin — `plugins/demo_render_feature_v2_second/`

Same shape, genuinely different configuration. Implements
`IRenderFeatureModule_v2`, declaring:
- `stage = PreUI`, `priority = 0`, `blendMode = AlphaOver`
- `AddRadialVignettePass("DemoRenderFeatureV2Second_Vignette", 0.5, 0.5,
  0.15, 0.65, 0.0, 0.0, 1.0, 1.0)` — a BLUE radial vignette, centered,
  opaque core fading to fully transparent by the frame's edge.

Content matches the phase file's own Step 3.2 code block verbatim.

### 3. Root `CMakeLists.txt` registration

Inside the existing `if(GTE_ENABLE_PLUGINS) ... endif()` block, appended
immediately AFTER `add_subdirectory(plugins/demo_editor_panel)`:

```cmake
add_subdirectory(plugins/demo_render_feature_v2)
add_subdirectory(plugins/demo_render_feature_v2_second)
```

**No other file was touched.** `git_status` before commit shows exactly:

```
modified:   CMakeLists.txt
untracked:  plugins/demo_render_feature_v2/
untracked:  plugins/demo_render_feature_v2_second/
```

`tools/ci/gte_plugin_isolation_probe/main.cpp`'s hardcoded
`LoadedModuleCount() != 4` check was deliberately left untouched — per
`PHASE8_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md` Step 3.3 item 4, that
probe's own count-of-6 update is explicitly Phase 8's job, not Phase 6's
(confirmed by direct read of the Phase 8 file before making this call, so no
`ask_questions` round-trip was needed for this specific point).

## Deviations from the plan

**None.** Every file matches `PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md`'s own
Step 3.1/3.2/3.3 code blocks verbatim, and the root `CMakeLists.txt` edit
lands in the exact position Step 3.3 specifies.

## Verification evidence

### 1. Incremental build

`cmake --build build` (Ninja/MinGW). **4/5 steps succeeded** on the first
attempt with zero compile errors:

```
[1/5] Building CXX object plugins/demo_render_feature_v2/CMakeFiles/demo_render_feature_v2.dir/RenderFeaturePlugin.cpp.obj
[2/5] Building CXX object plugins/demo_render_feature_v2_second/CMakeFiles/demo_render_feature_v2_second.dir/RenderFeaturePlugin.cpp.obj
[3/5] Linking CXX shared library plugins\demo_render_feature_v2_second.dll
[4/5] Linking CXX shared library plugins\demo_render_feature_v2.dll
```

(Only the expected pre-existing `MingwRuntime.cmake`/KTX-version warnings on
`stderr` for every plugin target, including the two new ones — no new
errors, no new warning classes.)

### 2. Live smoke test — both permanent `_v2` demo plugins loaded, alongside all 4 pre-existing `_v1`/panel demo plugins

Ran `build\GreatTamanaEditor.exe` (PID 1588).

**`GET /get_logs?limit=200`** — both new plugins loaded correctly:

```
[Info] PluginHost: "Loaded plugin 'DemoRenderFeatureV2Plugin' v1.0.0 from ...\plugins\demo_render_feature_v2.dll"
[Info] PluginHost: "Loaded plugin 'DemoRenderFeatureV2SecondPlugin' v1.0.0 from ...\plugins\demo_render_feature_v2_second.dll"
```

**No unwired-stage warning, no priority-collision warning** — each new
plugin declares a unique priority within its own distinct stage
(`DemoRenderFeatureV2` in `PostComposite`, `DemoRenderFeatureV2Second` in
`PreUI`, never colliding with each other). The pre-existing `_v1` "2 loaded
plugins implement IRenderFeatureModule_v1" warning is present, byte-for-byte
unchanged from every prior phase's own recorded baseline. The only other
warnings present are the pre-existing, unrelated
`sharedRuntimeLinkage=0` static-CRT notice (present at every prior phase
too) and the same cosmetic `RenderGraph` GPU-timing-slot-budget-exhaustion
warnings PHASE5 already documented for the new pass names
(`RenderFeatureCompositor_Scene_SeedCopy`, `DemoRenderFeatureV2_Scene_Blend`,
`DemoRenderFeatureV2Second_Scene_Blend`) — a harmless, pre-existing
`render-pass-6`-campaign mechanism, unrelated to this phase's own scope; GPU
timing simply reads "Absent" for those passes.

**`GET /get_game_view`** (349×155 PNG) and **`GET /get_swapchain`** — both
confirmed the SAME final composited image, visible simultaneously in both
the "Scene" AND "Game" panels (the compositor runs
`ProviderScope::PerActiveView`):

- Frame CENTER reads solid **BLUE**.
- Frame EDGES/corners read solid **RED** (`DemoRenderFeatureV2`'s own fill,
  unmodified — the vignette's alpha has fallen to 0 there).
- A soft red-to-blue gradient ring in between.

**`_v1`-vs-`_v2` interaction observed (Locked Design Decision #9, explicitly
non-deterministic/out-of-scope, documented here as required)**: the pixel the
render graph actually displayed this run was `_v2`'s own RED/BLUE result, not
`_v1`'s magenta — meaning `RenderFeatureCompositor`'s `AfterEverything`-tier
pass(es) executed AFTER `PluginRenderPassBuilderAdapter`'s (`_v1`'s) own pass
this particular run, for the SAME view/target handle (Locked Design Decision
#10 — `_v2`'s last blend write lands in the exact same `pluginTarget` handle
`_v1` also writes into). This is consistent with `PHASE4`/`PHASE5`'s own prior
completion reports, which both also observed `_v2`'s output winning over
`_v1`'s magenta with the exact same plugin set loaded — the relative
declaration/registration order between `LegacyRenderFeatureOrchestrator`
(`_v1`) and `RenderFeatureCompositor` (`_v2`) inside
`Core::RegisterBuiltinCapabilityOrchestrators()` is what actually determines
this, and that order has not changed since PHASE2, so this outcome is
expected and stable, not a fluke — but it remains, by explicit design, an
UNSPECIFIED interaction never to be treated as a bug (PHASE0 Locked Design
Decision #9).

### 3. Mathematical pixel-level confirmation (Step 3.4 point 4)

The returned `GET /get_game_view` PNG was saved to a temporary file and
decoded with a short, throwaway, NEVER-COMMITTED Python script (plain
`zlib`/`struct`, no third-party imaging library available on this machine —
confirmed `PIL`/`Pillow` is not installed) that manually parses the PNG
IHDR/IDAT chunks and reverses the per-scanline filter bytes to recover raw
RGBA8 pixel values. Both the script and the saved PNG were deleted
immediately after use (per Locked Design Decision #7 — no new automated
`ctest`, and per this phase's own "never committed" instruction for this
throwaway check).

Confirmed image size: **349×155** pixels.

Using the EXACT same formulas as `RenderFeatureOps.comp` (vignette falloff)
and `RenderFeatureBlend.comp` (`AlphaOver`: `result = mix(dst.rgb, src.rgb,
src.a)`), with `dst = RED (1,0,0)` (from `DemoRenderFeatureV2`'s own
`PostComposite` fill, `Replace`-composited onto the seed first) and
`src = BLUE (0,0,1)` with per-pixel alpha = the vignette's own radial
falloff (center = (0.5,0.5), innerRadius = 0.15, outerRadius = 0.65, in the
aspect-corrected UV space `RenderFeatureOps.comp` itself uses:
`aspectCorrectedUv.x *= size.x/size.y`, same scaling applied to the center):

| Label  | Pixel coord | Hand-computed expected RGB | Actual decoded RGB | Match |
|--------|-------------|-----------------------------|---------------------|-------|
| Center | (174, 77)   | uv = (0.5, 0.5) exactly == declared vignette center → `dist = 0` → `falloff = 1.0` (fully opaque blue) → **(0, 0, 255)** | **(0, 0, 255)** | ✅ exact |
| Corner | (0, 0)      | aspect-corrected `dist ≈ 1.2276` (well past `outerRadius = 0.65`) → `falloff = 0.0` (fill shows through unmodified) → **(255, 0, 0)** | **(255, 0, 0)** | ✅ exact |
| Ring   | (236, 77)   | aspect-corrected `dist ≈ 0.4002` (between `innerRadius`/`outerRadius`) → `falloff = 1 - (0.4002 - 0.15) / 0.5 ≈ 0.4996` → `mix(red, blue, 0.4996)` = **(≈127.6, 0, ≈127.4)**, i.e. **(128, 0, 127)** rounding either direction | **(127, 0, 128)** | ✅ within ±1 of 8-bit quantization (expected, since the GPU shader itself only has 8-bit UNORM output precision) |

All 3 expected-vs-actual comparisons match exactly (center/corner) or within
a single 8-bit quantization step (the gradient-ring point, where the
hand-computed 0.4996 falloff sits almost exactly on a rounding boundary
between 127 and 128) — this is a real, mathematically verified, per-pixel
confirmation that:

1. The real `AlphaOver` blend formula is genuinely computed per-pixel, not
   "last write wins" — the smooth gradient ring is only possible if `src.a`
   (the vignette's radial alpha falloff) is actually read and used to
   interpolate, pixel by pixel, exactly matching the shader's own formula.
2. `PostComposite` (`DemoRenderFeatureV2`'s RED fill) genuinely composites
   BEFORE `PreUI` (`DemoRenderFeatureV2Second`'s BLUE vignette) — a reversed
   order would show the OPPOSITE center/edge coloring.
3. Two simultaneously-loaded, genuinely-different `_v2` render-feature
   plugins (different colors AND different spatial patterns — not the
   same-color trick the original `_v1` demos used) really do composite
   correctly into one final image, replacing the Proposal's own documented
   embarrassment.

`stop_app_background` called at the end of the check (PID 1588).

### 4. `git_status`

Before commit, the diff is exactly:

```
modified:   CMakeLists.txt
untracked:  plugins/demo_render_feature_v2/
untracked:  plugins/demo_render_feature_v2_second/
```

— matching the phase file's own "touches only the 2 new plugin folders +
`CMakeLists.txt`" requirement exactly.

## What this phase does NOT do (confirmed honored)

- Does not edit any of the 4 existing demo plugins (`demo_hello_world`,
  `demo_render_feature`, `demo_render_feature_second`, `demo_editor_panel`) —
  confirmed via `git_status` showing zero modification to any of them.
- Does not touch the Render Graph panel — PHASE7's own job.
- Does not add any new automated `ctest` (PHASE0 Locked Design Decision #7) —
  the pixel-level proof above is a manual, throwaway, never-committed script
  run against a live engine instance, exactly as scoped.
- Does not touch `tools/ci/gte_plugin_isolation_probe/main.cpp`'s
  `LoadedModuleCount()` count check — explicitly Phase 8's job per that
  phase's own file (Step 3.3 item 4), confirmed by direct read before
  proceeding.

## Summary

`plugins/demo_render_feature_v2/` and
`plugins/demo_render_feature_v2_second/` are now real, permanent, committed
demo plugins proving the `_v2` render-feature system end-to-end: two
genuinely different colors (RED vs. BLUE), two genuinely different spatial
patterns (flat fill vs. radial vignette), two different stages
(`PostComposite` vs. `PreUI`), and two different blend modes (`Replace` vs.
`AlphaOver`), loaded simultaneously alongside the original 4 `_v1`-era demo
plugins with zero code change to any of them. The live, HTTP-driven,
mathematically-verified pixel comparison confirms the compositor's ordering
and blending are both genuinely correct, not merely "looks about right" —
closing out the Proposal's own documented embarrassment where the original
2 `_v1` demo plugins deliberately cleared to the identical color, hiding the
underlying "last write wins" bug from every prior screenshot-based smoke
test. PHASE7 can now proceed to surface this real, resolved ordering
decision (plugin name/priority/blend mode per stage) in the Editor's "Render
Graph" panel.
