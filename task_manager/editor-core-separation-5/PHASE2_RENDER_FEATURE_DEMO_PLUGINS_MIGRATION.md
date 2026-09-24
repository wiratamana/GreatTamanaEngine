# PHASE2 — Migrate `demo_render_feature` + `demo_render_feature_second` onto the new authoring sugar

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`'s own `PHASE1_COMPLETION_REPORT.md` (confirms the two new headers exist
and their exact final content/location) before starting.

**Use `ask_questions` if anything below is ambiguous or looks wrong once you have
the real files open — do not silently guess.**

---

## Step 1: The Goal

Rewrite `plugins/demo_render_feature/RenderFeaturePlugin.cpp` and
`plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` to use Phase 1's
`SingleCapabilityPluginModule<IRenderFeatureModule_v1>` +
`GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE`, removing the hand-written
`Demo*PluginModule : public IPluginModule` glue class and the hand-written
`extern "C" { ... }` block from both files, while producing **byte-identical
observable behavior**: same `GtePluginModuleInfo` (`name`/`version`/
`description`), same render pass name string, same capability query string.

## Step 2: The Situation

Current `plugins/demo_render_feature/RenderFeaturePlugin.cpp` (67 lines) and
`plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` (75 lines) each
define: a `DemoRenderFeature`/`DemoRenderFeatureSecond` class implementing
`IRenderFeatureModule_v1::AddRenderGraphPasses()` (the ONLY real logic in either
file — a single `builder.AddFullscreenClearPass(...)` call), a
`Demo*PluginModule : public IPluginModule` glue class (manual
`QueryCapability()` string-compare + manual `GetModuleInfo()` with 3
`std::strncpy` calls), and a verbatim `extern "C"` export block. Read both files
in full with `read_file` before editing, to have the EXACT current string
literals in front of you (do not retype them from memory/this document — copy
them character for character):

- `demo_render_feature`: pass name `"DemoRenderFeaturePlugin_Clear"`, module
  name `"DemoRenderFeaturePlugin"`, version `"1.0.0"`, description
  `"Milestone 1 proof - clears the Game/Scene View to solid magenta."`.
- `demo_render_feature_second`: pass name
  `"DemoRenderFeatureSecondPlugin_Clear"`, module name
  `"DemoRenderFeaturePluginSecond"`, version `"1.0.0"`, description
  `"editor-core-separation-4 PHASE5 proof - a SECOND plugin implementing IRenderFeatureModule_v1, clearing to the same magenta as the first, to exercise the 2-plugin path."`.

**`"DemoRenderFeaturePlugin_Clear"` is referenced by name in
`src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp`'s own comment** (confirmed
via `search_in_dir` — that file's comment says the Editor's "Render Graph" panel
shows this exact string) — this string must not change even by one character.

Both plugins' own `CMakeLists.txt` files (`plugins/demo_render_feature/
CMakeLists.txt`, `plugins/demo_render_feature_second/CMakeLists.txt`) list only
their own `.cpp` as the source, link `gte_plugin_abi` `PRIVATE`, and call
`gte_apply_plugin_dll_shared_crt_linkage(...)` — **none of this needs to
change**, only the `.cpp` content.

## Step 3: The Plan

### 3.1 — Rewrite `plugins/demo_render_feature/RenderFeaturePlugin.cpp`

Replace the ENTIRE file content with:

```cpp
// plugins/demo_render_feature/RenderFeaturePlugin.cpp
//
// PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md - Milestone 1's own throwaway
// proof: implements IRenderFeatureModule_v1, contributing exactly one
// render-graph pass that clears the Game/Scene View to solid magenta - a
// distinctive, unmistakable color no real production pass in this engine
// uses today (confirmed via search_in_dir on every existing clear-color
// constant before picking this - kGameClearColor is (20,20,30)/255, nothing
// close to solid magenta), so this phase's own visual smoke test can never
// be confused with a real rendering bug or a pre-existing pass.
//
// editor-core-separation-5 campaign, PHASE2
// (PHASE2_RENDER_FEATURE_DEMO_PLUGINS_MIGRATION.md) - migrated onto
// SingleCapabilityPluginModule<T> + GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE
// (see PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md) - removes the
// hand-written IPluginModule glue class and extern "C" block; every string
// literal below (pass name, module name/version/description) is byte-for-byte
// identical to this file's pre-migration content.

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

class DemoRenderFeature final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) override
    {
        builder.AddFullscreenClearPass("DemoRenderFeaturePlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f);
    }
};

DemoRenderFeature g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v1> g_module(
    g_feature, kIRenderFeatureModule_v1_Name,
    MakeModuleInfo("DemoRenderFeaturePlugin", "1.0.0", "Milestone 1 proof - clears the Game/Scene View to solid magenta."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
```

### 3.2 — Rewrite `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp`

Same shape, swap only the class name, pass name string, and
`GtePluginModuleInfo` fields (copy the EXACT current strings from Step 2 above —
do not paraphrase the description string):

```cpp
// plugins/demo_render_feature_second/RenderFeaturePlugin.cpp
//
// editor-core-separation-4 campaign, PHASE5
// (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
// SECOND, genuinely independent throwaway demo plugin implementing
// IRenderFeatureModule_v1, mirroring plugins/demo_render_feature/
// RenderFeaturePlugin.cpp's exact shape, only changing the class/info names
// and the pass name string. Deliberately clears to the exact SAME solid
// magenta color the first demo already uses, so the existing,
// extensively-documented "solid magenta Game/Scene View" visual baseline
// stays visually unchanged with this plugin added.
//
// editor-core-separation-5 campaign, PHASE2
// (PHASE2_RENDER_FEATURE_DEMO_PLUGINS_MIGRATION.md) - migrated onto
// SingleCapabilityPluginModule<T> + GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE,
// mirroring demo_render_feature's own migration exactly. Every string literal
// below is byte-for-byte identical to this file's pre-migration content.

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

class DemoRenderFeatureSecond final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) override
    {
        builder.AddFullscreenClearPass("DemoRenderFeatureSecondPlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f);
    }
};

DemoRenderFeatureSecond g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v1> g_module(
    g_feature, kIRenderFeatureModule_v1_Name,
    MakeModuleInfo("DemoRenderFeaturePluginSecond", "1.0.0",
        "editor-core-separation-4 PHASE5 proof - a SECOND plugin implementing "
        "IRenderFeatureModule_v1, clearing to the same magenta as the first, "
        "to exercise the 2-plugin path."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
```

**Before finalizing this file**, `read_file` the CURRENT
`plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` one more time and
diff the description string character-for-character against what is written
above (it was transcribed once already during this campaign's own planning pass
— re-verify it against the real file, do not trust this document blindly, per
`PHASE0_MASTER_STRATEGY.md`'s Workflow Rule 9).

### 3.3 — Do not touch either plugin's `CMakeLists.txt`

Confirm via `read_file` that both still only need `SHARED` source list = their
one `.cpp`, `target_link_libraries(... PRIVATE gte_plugin_abi)`,
`RUNTIME_OUTPUT_DIRECTORY`, `PREFIX ""`, and
`gte_apply_plugin_dll_shared_crt_linkage(...)` — none of this changes.

Run `git_status` before Step 3.1 (`PHASE0_MASTER_STRATEGY.md` Workflow Rule
10) — confirm the branch still reads `feature/editor-core-separation` and the
tree is clean or contains only Phase 1's already-committed diff.

## Verification

1. **Rebuild the isolation probe's own dedicated inner build** (per
   `PHASE0_MASTER_STRATEGY.md`'s "Reference commands" / Workflow Rule 2):
   ```
   cmake --build build-plugin-isolation-probe\gte_plugin_isolation_inner_build
   ```
   This is a genuine Ninja incremental build — it recompiles ONLY
   `demo_render_feature.dll`/`demo_render_feature_second.dll` (the two files
   this phase changed) plus re-links `gte_plugin_isolation_probe.exe` against
   them, without touching the main `build/` tree at all.
2. Confirm zero compile/link errors.
3. Run `build-plugin-isolation-probe\gte_plugin_isolation_inner_build\
   gte_plugin_isolation_probe.exe` directly (`run_shell`) and confirm:
   - Exit code `0`.
   - stdout contains `PASS: 4 plugin(s) loaded, exactly 2 implement
     IRenderFeatureModule_v1, ...` (the exact count assertions inside
     `main.cpp` — `LoadedModuleCount() == 4` and `renderFeatureCount == 2` — must
     both still hold; if either fails, that is a REAL regression this phase
     introduced, not a probe bug — go find and fix it before proceeding).
   - The two `Loaded '...' - IRenderFeatureModule_v1: yes` lines still print
     `DemoRenderFeaturePlugin` and `DemoRenderFeaturePluginSecond` (the exact
     `info.name` values) — confirms `MakeModuleInfo()`'s bounded copy produced
     byte-identical names to the original hand-written `std::strncpy` calls.
4. Also rebuild the MAIN `build/` tree's own plugin targets (`cmake --build
   build --target demo_render_feature demo_render_feature_second`) so the
   `.dll`s sitting in `build/plugins/` are current for Phase 3/5's own later use
   of the live Editor — this is still a small, targeted incremental build, not
   a full rebuild (do not build the whole `build` target list here).

## Completion

Write `PHASE2_COMPLETION_REPORT.md`: both rewritten files' exact final content,
the isolation probe's exact stdout, explicit confirmation every string literal
diffed identical to the pre-migration original, and the line-count reduction
achieved (67→~20, 75→~28, roughly). Run `git_status` (Workflow Rule 10) and
confirm the diff about to be staged is EXACTLY the two rewritten `.cpp` files
(plus the `PHASE2_COMPLETION_REPORT.md` you are about to add) — nothing else.
Then `git_add` + `git_commit` (message referencing PHASE2).
