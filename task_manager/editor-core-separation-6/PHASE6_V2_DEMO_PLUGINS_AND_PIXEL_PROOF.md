# PHASE6 — Permanent `_v2` Demo Plugins + Real Pixel-Level Compositing Proof

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Design Decisions #7, #8, #9). Also read `PHASE4`/`PHASE5` completion reports
before starting.

## Step 1: The Goal

Ship the two PERMANENT, COMMITTED `_v2` demo plugins (never deleted, unlike
PHASE4/PHASE5's own throwaway scratch probes), and produce the real,
pixel-level, live-engine proof
(`RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md` Section
3.7) that 2 simultaneously-loaded `_v2` render-feature plugins genuinely
composite correctly — replacing the Proposal's own documented embarrassment
(the 2 existing `_v1` demo plugins deliberately clear to the identical
color, hiding the original bug from every screenshot-based smoke test ever
run).

## Step 2: The Situation

PHASE0 Locked Design Decision #8: two BRAND-NEW plugin folders,
`plugins/demo_render_feature_v2/` and
`plugins/demo_render_feature_v2_second/`. All 4 existing demo plugins
(`demo_hello_world`, `demo_render_feature`, `demo_render_feature_second`,
`demo_editor_panel`) stay 100% untouched — this phase edits NONE of them.

PHASE0 Locked Design Decision #9: `_v1` and `_v2` render-feature plugins are
NOT unified into one deterministic order — both `demo_render_feature`/
`demo_render_feature_second` (`_v1`, solid magenta) and the two new `_v2`
plugins stay loaded together in the SAME `plugins/` folder. Document in this
phase's own completion report which one happened to win the final pixel
this run (an accepted, unspecified, out-of-scope interaction per Locked
Design Decision #9 — never treated as a bug to fix here).

`plugins/gte_plugin_abi/SingleCapabilityPluginModule.h`'s
`SingleCapabilityPluginModule<CapabilityInterface>` and
`plugins/gte_plugin_abi/PluginExportsMacro.h`'s
`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE` are the exact, established
authoring pattern every `_v1` demo plugin already uses
(`plugins/demo_render_feature/RenderFeaturePlugin.cpp`, confirmed by direct
read) — the new `_v2` demo plugins use the exact same pattern, templated on
`IRenderFeatureModule_v2` instead of `IRenderFeatureModule_v1`.

PHASE0 Locked Design Decision #7: no new automated `ctest` — the proof is a
manual, live-engine, HTTP-driven procedure using `gte_send_request`.

## Step 3: The Plan

### Step 3.1 — New file: `plugins/demo_render_feature_v2/RenderFeaturePlugin.cpp` + `CMakeLists.txt`

Mirrors `plugins/demo_render_feature/CMakeLists.txt`'s exact shape (`add_library(...
SHARED)`, `target_link_libraries(... PRIVATE gte_plugin_abi)`,
`RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"`, `PREFIX ""`,
`gte_apply_plugin_dll_shared_crt_linkage(...)`), only the target/plugin name
changes.

```cpp
#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/RenderFeatureDescriptor.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder_v2.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

// editor-core-separation-6 campaign, PHASE6
// (PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md) - a real, DISTINCT-from-the-
// other-_v2-demo-plugin proof: an opaque RED solid fill, stage
// PostComposite, priority 0, blendMode Replace - deliberately NOT magenta
// (this repo's existing _v1 baseline color) and NOT the same color/blend
// mode as demo_render_feature_v2_second, so this campaign never repeats
// the exact mistake RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_
// 2026-09-25.md Part 1.5 documents.
class DemoRenderFeatureV2 final : public IRenderFeatureModule_v2 {
public:
    GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const override
    {
        return MakeRenderFeatureDescriptor("DemoRenderFeatureV2",
            RenderFeatureStage::PostComposite, /*priority=*/0, RenderFeatureBlendMode::Replace);
    }

    void AddRenderGraphPasses(IPluginRenderPassBuilder_v2& builder) override
    {
        builder.AddSolidFillPass("DemoRenderFeatureV2_Fill", 1.0f, 0.0f, 0.0f, 1.0f);
    }
};

DemoRenderFeatureV2 g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v2> g_module(
    g_feature, kIRenderFeatureModule_v2_Name,
    MakeModuleInfo("DemoRenderFeatureV2Plugin", "1.0.0",
        "editor-core-separation-6 proof - an opaque RED solid fill, PostComposite stage, "
        "priority 0, Replace blend mode."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
```

### Step 3.2 — New file: `plugins/demo_render_feature_v2_second/RenderFeaturePlugin.cpp` + `CMakeLists.txt`

Same shape, GENUINELY different configuration — same 5 `#include` lines as
Step 3.1 above (`IRenderFeatureModule.h`, `RenderFeatureDescriptor.h`,
`IPluginRenderPassBuilder_v2.h`, `SingleCapabilityPluginModule.h`,
`PluginExportsMacro.h`), same `namespace gte { namespace { ... } }` wrapping
(the class, the `g_feature`/`g_module` globals, and the trailing
`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)` call all sit at
the exact same nesting level Step 3.1's own file uses):
```cpp
class DemoRenderFeatureV2Second final : public IRenderFeatureModule_v2 {
public:
    GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const override
    {
        return MakeRenderFeatureDescriptor("DemoRenderFeatureV2Second",
            RenderFeatureStage::PreUI, /*priority=*/0, RenderFeatureBlendMode::AlphaOver);
    }

    void AddRenderGraphPasses(IPluginRenderPassBuilder_v2& builder) override
    {
        // A BLUE radial vignette, centered, opaque core fading to fully
        // transparent by the frame's edge - genuinely distinguishable from
        // the first plugin's own flat, uniform RED fill both in COLOR and
        // in SPATIAL pattern (not just a different flat color - see this
        // phase's own Step 3.4 for exactly why this specific pair was
        // chosen, mirroring PHASE5's own stage-A/stage-B scratch-probe
        // pattern, now made PERMANENT).
        builder.AddRadialVignettePass("DemoRenderFeatureV2Second_Vignette",
            0.5f, 0.5f, 0.15f, 0.65f, 0.0f, 0.0f, 1.0f, 1.0f);
    }
};

DemoRenderFeatureV2Second g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v2> g_module(
    g_feature, kIRenderFeatureModule_v2_Name,
    MakeModuleInfo("DemoRenderFeatureV2SecondPlugin", "1.0.0",
        "editor-core-separation-6 proof - a BLUE radial vignette, PreUI stage, priority 0, "
        "AlphaOver blend mode - a SECOND, genuinely distinct _v2 plugin proving real "
        "multi-plugin compositing (not the same color trick the original _v1 demos used)."));
} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
```

### Step 3.3 — Root `CMakeLists.txt` registration

Inside the existing `if(GTE_ENABLE_PLUGINS) ... endif()` block (Step 2 of
this campaign's research already located this block's exact current
content — `demo_hello_world`, `demo_render_feature`,
`demo_render_feature_second`, `demo_editor_panel`, in that order), append,
AFTER `add_subdirectory(plugins/demo_editor_panel)`:

```cmake
# editor-core-separation-6 campaign, PHASE6
# (PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md) - the two PERMANENT _v2
# render-feature demo plugins, proving real multi-plugin compositing with
# two genuinely different colors/blend modes (never the same-color trick
# the original _v1 demos used - see RENDER_FEATURE_COMPOSITING_FINDINGS_
# AND_PROPOSAL_2026-09-25.md Part 1.5).
add_subdirectory(plugins/demo_render_feature_v2)
add_subdirectory(plugins/demo_render_feature_v2_second)
```

### Step 3.4 — The real, manual, live-engine pixel-level proof

1. `run_app_background` on `build\GreatTamanaEditor.exe`.
2. `gte_send_request` against `GET /get_swapchain` (or `GET /get_game_view`
   — confirm via `PHASE0`'s own reference-command list which endpoint
   actually returns the FINAL, on-screen composited image vs. an
   intermediate debug texture; prefer whichever one this repo's own prior
   completion reports, e.g. `editor-core-separation-3/4`'s Game View smoke
   tests, already established as "the true final displayed image").
3. The tool returns the frame directly as a viewable image
   (`gte_send_request`'s own documented behavior — auto-decodes either raw
   image bytes or a base64 JSON envelope). Visually confirm, and describe
   in the completion report:
   - Frame CENTER reads solid BLUE (or very close to it) — the vignette's
     own opaque core, `AlphaOver`-composited on top of whatever the RED
     fill (and/or the pre-existing `_v1` magenta clear, depending on which
     orchestrator happened to run last for THIS pixel this frame — Locked
     Design Decision #9) left underneath.
   - Frame EDGES/corners read solid RED (the vignette's alpha has fallen to
     0 there, so the underlying RED fill shows through unmodified) — OR,
     if `_v1`'s magenta happened to win at the edges instead (an accepted,
     unspecified `_v1`-vs-`_v2` interaction), document that explicitly and
     explain WHY it happened (which orchestrator's pass the render graph
     actually scheduled last, and why) rather than silently ignoring the
     discrepancy.
   - A soft red-to-blue (or magenta-to-blue) gradient ring in between,
     proving the vignette's own falloff math is genuinely per-pixel, not a
     hard binary edge.
4. **Mathematical confirmation, not just an eyeball check**: pick 3 concrete
   pixel coordinates — dead center, a point known to be inside
   `outerRadius` but outside `innerRadius` (the gradient ring), and a
   corner — and, using the EXACT same `AlphaOver` formula
   `result = mix(dst.rgb, src.rgb, src.a)` and the vignette's own falloff
   formula from `RenderFeatureOps.comp`/`RenderFeatureBlend.comp` (PHASE4/
   PHASE5), hand-compute (or write a short throwaway Python/PowerShell
   one-liner — never committed) the EXPECTED RGB value at each of those 3
   coordinates given the known RED fill color and known vignette
   parameters, and compare against the ACTUAL decoded pixel value read from
   the captured image at those exact coordinates. Record all 3 expected-
   vs-actual comparisons in the completion report — this is what makes this
   phase's proof "real, pixel-level, mathematically verified" rather than
   "looks about right."
5. `GET /get_logs?limit=100` — confirm NO unexpected `RenderFeatureCompositor`
   warning fired (both new plugins use distinct `priority` values within
   their own distinct stages, so no collision warning should appear; both
   use wired stages, so no unwired-stage warning should appear either).
6. `stop_app_background` the PID when finished.

### Verification

1. Incremental build: `cmake --build build`.
2. The full Step 3.4 procedure above, end to end.
3. `git_status` — confirm the diff touches only the 2 new plugin folders +
   `CMakeLists.txt`.

### What this phase does NOT do

- Does not edit any of the 4 existing demo plugins.
- Does not touch the Render Graph panel (PHASE7).
- Does not add any new automated `ctest` (PHASE0 Locked Design Decision #7).

### Completion

Write `PHASE6_COMPLETION_REPORT.md` (the full Step 3.4 procedure's real
results: the captured image reference, the 3 expected-vs-actual pixel
comparisons, the `_v1`-vs-`_v2` interaction observed and explained, the
confirmed-clean log output), then `git_add` + `git_commit`.
