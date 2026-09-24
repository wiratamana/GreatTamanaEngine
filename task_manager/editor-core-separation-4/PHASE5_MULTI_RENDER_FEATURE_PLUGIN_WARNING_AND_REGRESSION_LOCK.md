# PHASE5 — Multi-Render-Feature-Plugin Warning + Regression Lock

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`-`PHASE4`'s own `COMPLETION_REPORT.md` files if they exist (Phase 4 in
particular changed which CMake helper function every demo plugin calls —
match that same helper for the new plugin this phase adds).

**Severity:** MEDIUM (Issue #5 of the re-analysis document).

**Source of truth for this bug:**
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\editor-core-separation\
editor-core-separation-3_REANALYSIS_bugs_and_gaps_2026-09-24.md`, section "5.
MEDIUM — 'one or more plugins may contribute a render pass' is only tested with
exactly one".

---

## Step 1: The Goal

Today, if 2+ loaded plugins each implement `IRenderFeatureModule_v1`,
`Core::RegisterOffscreenRenderPipelineProviders()`'s `"PluginRenderFeatures"`
provider hands EVERY one of them the exact same shared render-target handle —
they silently overwrite each other's output, with the last-registered plugin
winning, no error, no log, no test. This directly contradicts this whole
system's own stated design ("a feature can ship as one **or more** `.dll`s") —
the plural case was never actually exercised (every probe and the live smoke
test ship exactly one such plugin).

Design decision already made (confirmed via `ask_questions`, do not
re-litigate): **minimal fix only — no new render-target/compositing work.**
Concretely:

1. A new, small, pure, Tier-1-testable function counts how many loaded
   modules implement `IRenderFeatureModule_v1`.
2. `Core` logs one clear `GTE_LOG_WARNING` when that count is `> 1`,
   explicitly naming the real, current "last one wins, silently" behavior so
   it is at least visible instead of invisible.
3. A second, genuinely independent, real, throwaway demo plugin
   (`demo_render_feature_second`) is added, so this 2-plugin path is
   EXERCISED for real by this engine's own live smoke test and probes — not
   just theorized about. It deliberately clears to the exact SAME solid
   magenta color `demo_render_feature` already uses (see Step 3.2 for why),
   so the documented "solid magenta Game/Scene View" visual baseline every
   prior campaign's own smoke test already relies on is genuinely unchanged
   by adding it.
4. `gte_plugin_isolation_probe` is updated to expect 4 loaded plugins and 2
   implementing `IRenderFeatureModule_v1` (was 3 and 1).

## Step 2: The Situation (exact current code)

`src/Core/Core.cpp`'s `"PluginRenderFeatures"` provider (inside
`RegisterOffscreenRenderPipelineProviders()`, around line 724):

```cpp
m_offscreenRenderPipeline.Register("PluginRenderFeatures", rg::ProviderScope::PerActiveView,
    [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
        const RenderPassViewData* viewData = FindViewData(frame.currentView);
        if (viewData == nullptr) {
            return;
        }

        const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
        const rg::RenderPassId compositedKey = isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey;
        const rg::TextureHandle pluginTarget =
            frame.blackboard.Fetch<rg::TextureHandle>(compositedKey).value_or(viewData->colorTarget);

        bool anyPluginFeatureRanThisView = false;
        for (IPluginModule* module : m_pluginHost.AllLoadedModules()) {
            if (auto* feature = static_cast<IRenderFeatureModule_v1*>(
                    module->QueryCapability(kIRenderFeatureModule_v1_Name))) {
                PluginRenderPassBuilderAdapter adapter(frame.builder, pluginTarget);
                feature->AddRenderGraphPasses(adapter);
                anyPluginFeatureRanThisView = true;
            }
        }
        if (anyPluginFeatureRanThisView) {
            frame.finalTextureOutputs.push_back(pluginTarget);
        }
    },
    rg::ProviderTiming::AfterDeferredPasses);
```

`Core::LoadPlugins()` (`src/Core/Core.cpp`, line ~220):
```cpp
void Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    m_pluginHost.LoadPlugins(pluginsDirectory);
}
```

`plugins/demo_render_feature/RenderFeaturePlugin.cpp` — the one existing demo
(read it in full before writing the new one, to mirror its exact shape byte
for byte, only changing the class/info names — its `GetModuleInfo()` returns
name `"DemoRenderFeaturePlugin"`, its clear color is solid magenta `(1,0,1,1)`
via `AddFullscreenClearPass("DemoRenderFeaturePlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f)`).

`tools/ci/gte_plugin_isolation_probe/main.cpp` currently asserts
`LoadedModuleCount() == 3` and `renderFeatureCount == 1`.

## Step 3: The Plan (exact changes)

### 3.1 New Tier-1-testable counting function

Create a new small header, `src/Core/Plugins/PluginRenderFeatureDiagnostics.h`
(header + matching `.cpp`, mirroring this codebase's own small-single-purpose-
file convention, e.g. `PluginRenderPassBuilderAdapter.h/.cpp`):

```cpp
// src/Core/Plugins/PluginRenderFeatureDiagnostics.h
#pragma once

#include <vector>

namespace gte {

class IPluginModule;

// editor-core-separation-4 campaign, PHASE5
// (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
// small, pure, Tier-1-testable helper: counts how many of the given loaded
// plugin modules implement IRenderFeatureModule_v1. Takes a plain vector of
// already-resolved IPluginModule* (never touches PluginHost/the filesystem/
// any GPU state itself), so it is directly unit-testable with fake
// IPluginModule doubles - mirrors this codebase's own established "extract
// the pure logic, test it directly" precedent (AGENTS.md, "Testability &
// Regression Safety").
int CountModulesImplementingRenderFeature(const std::vector<IPluginModule*>& modules);

} // namespace gte
```

```cpp
// src/Core/Plugins/PluginRenderFeatureDiagnostics.cpp
#include "PluginRenderFeatureDiagnostics.h"

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"

namespace gte {

int CountModulesImplementingRenderFeature(const std::vector<IPluginModule*>& modules)
{
    int count = 0;
    for (IPluginModule* module : modules) {
        if (module != nullptr && module->QueryCapability(kIRenderFeatureModule_v1_Name) != nullptr) {
            ++count;
        }
    }
    return count;
}

} // namespace gte
```

Add both files to root `CMakeLists.txt`'s `gte_core` unconditional source list
(right next to the existing `src/Core/Plugins/PluginHost.h/.cpp` /
`PluginRenderPassBuilderAdapter.h/.cpp` entries — same neighborhood, same
comment style pointing at this phase).

### 3.2 `Core` logs one warning, once, if the count is `> 1`

File: `src/Core/Core.cpp`, inside `Core::LoadPlugins()`:

```cpp
void Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    m_pluginHost.LoadPlugins(pluginsDirectory);

    // editor-core-separation-4 campaign, PHASE5
    // (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) -
    // this system's own render-graph integration ("PluginRenderFeatures"
    // provider, RegisterOffscreenRenderPipelineProviders() below) hands
    // EVERY loaded IRenderFeatureModule_v1 the SAME shared render target -
    // with 2+ such plugins loaded, only the last-registered one's output
    // ends up visible (a silent overwrite, by design of the CURRENT minimal
    // implementation - real per-plugin compositing is explicitly deferred,
    // see this phase's own file). Log this loudly, once, so it is at least
    // a known, visible fact instead of a silent surprise.
    const int renderFeatureModuleCount = CountModulesImplementingRenderFeature(m_pluginHost.AllLoadedModules());
    if (renderFeatureModuleCount > 1) {
        GTE_LOG_WARNING("PluginHost",
            std::to_string(renderFeatureModuleCount) + " loaded plugins implement IRenderFeatureModule_v1 - "
            "only the LAST-registered one's render output will be visible this frame (render-graph "
            "compositing for multiple render-feature plugins is not implemented - see "
            "docs/conventions/plugin-architecture.md).");
    }
}
```

Add `#include "Plugins/PluginRenderFeatureDiagnostics.h"` to `Core.cpp`'s
existing include list.

### 3.3 New throwaway demo plugin: `demo_render_feature_second`

Create `plugins/demo_render_feature_second/CMakeLists.txt` (copy
`plugins/demo_render_feature/CMakeLists.txt` verbatim, renaming the target),
and `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` (copy
`plugins/demo_render_feature/RenderFeaturePlugin.cpp` verbatim), with exactly
these differences:

- Class names: `DemoRenderFeatureSecond` / `DemoRenderFeatureSecondPluginModule`
  (avoid an ODR collision if these two DLLs were ever statically linked
  together anywhere — they never are, but keep the names distinct anyway as
  good practice, matching this codebase's own care elsewhere).
- `GetModuleInfo()`'s `name` field: `"DemoRenderFeaturePluginSecond"`.
- `GetModuleInfo()`'s `description` field: something distinguishing it as
  this phase's own second-plugin proof, e.g. `"editor-core-separation-4
  PHASE5 proof - a SECOND plugin implementing IRenderFeatureModule_v1,
  clearing to the same magenta as the first, to exercise the 2-plugin path."`
- The `AddFullscreenClearPass()` call KEEPS the exact same color
  (`1.0f, 0.0f, 1.0f, 1.0f`, solid magenta) and a distinct pass name string
  (`"DemoRenderFeatureSecondPlugin_Clear"`, since `RenderGraphBuilder`
  requires unique pass names within a frame — verify this by reading
  `IPluginRenderPassBuilder`/`PluginRenderPassBuilderAdapter.cpp`'s own
  `AddFullscreenClearPass()` implementation before assuming, and use
  `ask_questions` if this pass-name uniqueness requirement turns out to
  conflict with anything).
- **Do NOT** change the clear color — this is a deliberate choice (confirmed
  in Step 1) so the existing, extensively-documented "solid magenta Game/
  Scene View" visual baseline every prior campaign's live smoke test already
  established stays visually unchanged with this plugin added, regardless of
  which of the two plugins ends up "winning" per-frame.
- Its own CMakeLists.txt calls `gte_apply_plugin_dll_shared_crt_linkage(...)`
  (Phase 4's new function name) rather than the original — if Phase 4 has not
  landed yet when you implement this phase, use the ORIGINAL
  `gte_apply_plugin_shared_crt_linkage(...)` name instead and leave a
  `// TODO(Phase4): switch to gte_apply_plugin_dll_shared_crt_linkage once
  PHASE4 lands` comment, then it's Phase 4's own job (if it runs after this
  one) to update it — check this campaign's own phase completion reports to
  see which phase actually ran first, since phases in this campaign may be
  implemented out of the strict severity order during the second iteration.

### 3.4 Wire the new plugin into root `CMakeLists.txt`

Inside the SAME `if(GTE_ENABLE_PLUGINS)` block Phase 2 already split out
(Step 3.1 of `PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md`), add:
```cmake
add_subdirectory(plugins/demo_render_feature_second)
```
right after the existing `add_subdirectory(plugins/demo_render_feature)`
line.

Also update `tools/ci/gte_plugin_isolation_probe/CMakeLists.txt`'s host-side
root `add_dependencies(gte_plugin_isolation_probe demo_hello_world
demo_render_feature demo_editor_panel)` line (found in the ROOT
`CMakeLists.txt`, not the probe's own small wrapper file — search for
`add_dependencies(gte_plugin_isolation_probe` in root `CMakeLists.txt`) to
also depend on `demo_render_feature_second`.

### 3.5 Update `tools/ci/gte_plugin_isolation_probe/main.cpp`

Change:
```cpp
if (host.LoadedModuleCount() != 3) {
    std::fprintf(stderr, "FAIL: expected exactly 3 demo plugins to load, loaded %zu.\n", host.LoadedModuleCount());
    return 1;
}
```
to expect `4`, and:
```cpp
if (renderFeatureCount != 1) {
    std::fprintf(stderr, "FAIL: expected exactly 1 plugin implementing IRenderFeatureModule_v1, found %d.\n", renderFeatureCount);
    return 1;
}
```
to expect `2`. Update the file's own top-of-file doc comment and the final
`PASS:` message string accordingly (both currently say "exactly 1" / "3
demo plugins" in prose, not just in the asserted numbers — keep the prose and
the real assertion in sync).

### 3.6 Add a Tier-1 test for the new counting function

Create `tests/Core/PluginRenderFeatureDiagnosticsTests.cpp`, mirroring
`tests/Core/EditorPanelRegistryTests.cpp`'s own `Fake*` test-double style:

```cpp
#include "Core/Plugins/PluginRenderFeatureDiagnostics.h"
#include "../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "../../plugins/gte_plugin_abi/IRenderFeatureModule.h"

#include <gtest/gtest.h>

#include <cstring>

namespace gte {
namespace {

class FakeRenderFeature final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder&) override { }
};

class FakeModuleWithRenderFeature final : public IPluginModule {
public:
    void* QueryCapability(const char* nameAndVersion) override
    {
        if (std::strcmp(nameAndVersion, kIRenderFeatureModule_v1_Name) == 0) {
            return &m_feature;
        }
        return nullptr;
    }
    void GetModuleInfo(GtePluginModuleInfo&) const override { }

private:
    FakeRenderFeature m_feature;
};

class FakeModuleWithoutRenderFeature final : public IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; }
    void GetModuleInfo(GtePluginModuleInfo&) const override { }
};

TEST(PluginRenderFeatureDiagnosticsTest, CountModulesImplementingRenderFeature_EmptyVectorReturnsZero)
{
    EXPECT_EQ(CountModulesImplementingRenderFeature({}), 0);
}

TEST(PluginRenderFeatureDiagnosticsTest, CountModulesImplementingRenderFeature_CountsOnlyModulesThatImplementIt)
{
    FakeModuleWithRenderFeature withFeatureA;
    FakeModuleWithRenderFeature withFeatureB;
    FakeModuleWithoutRenderFeature without;

    const std::vector<IPluginModule*> modules = { &withFeatureA, &without, &withFeatureB };

    EXPECT_EQ(CountModulesImplementingRenderFeature(modules), 2);
}

TEST(PluginRenderFeatureDiagnosticsTest, CountModulesImplementingRenderFeature_ZeroWhenNoneImplementIt)
{
    FakeModuleWithoutRenderFeature withoutA;
    FakeModuleWithoutRenderFeature withoutB;

    const std::vector<IPluginModule*> modules = { &withoutA, &withoutB };

    EXPECT_EQ(CountModulesImplementingRenderFeature(modules), 0);
}

} // namespace
} // namespace gte
```

Note: `IPluginRenderPassBuilder` only needs a forward declaration/include for
`AddRenderGraphPasses()`'s signature — include
`../../plugins/gte_plugin_abi/IPluginRenderPassBuilder.h` too if the compiler
requires the complete type for the override signature (it does, for a
reference parameter's method signature to compile) — check
`IRenderFeatureModule.h`'s own includes to confirm the exact header path
needed before assuming.

Add this new test file to `tests/CMakeLists.txt`'s source list, in the `Core/`
grouping alongside `Core/EditorPanelRegistryTests.cpp`.

### 3.7 Update `docs/conventions/plugin-architecture.md`

Add one short paragraph documenting the current, real, minimal behavior:
"multiple plugins may implement `IRenderFeatureModule_v1`; today, they all
render into the same shared target, and only the last-registered plugin's
output ends up visible — `Core::LoadPlugins()` logs a `GTE_LOG_WARNING` when
more than one is detected. Per-plugin compositing is explicitly deferred, not
yet designed."

## Verification (fast, incremental — no full build)

1. `cmake --build build --target gte_core` then `cmake --build build`
   (incremental — new CMake subdirectory + new source files require a
   configure step first: run a plain `cmake -S . -B build` reconfigure before
   the incremental build, still without deleting `build/`).
2. `cmake --build build --target GreatTamanaEngineTests` then run
   `build\GreatTamanaEngineTests.exe
   --gtest_filter=PluginRenderFeatureDiagnosticsTest.*` — confirm all 3 new
   tests pass.
3. Live check: `run_app_background` the rebuilt `GreatTamanaEditor.exe`.
   - `gte_send_request` `GET /get_logs?limit=50` and confirm a `"PluginHost"`
     warning containing `"2 loaded plugins implement IRenderFeatureModule_v1"`
     (or your exact wording) is present.
   - `gte_send_request` `GET /get_swapchain` and visually confirm (via
     `load_image` on the returned/saved frame, or by reading the tool's own
     returned image content) the Game/Scene View is STILL solid magenta,
     completely unchanged from every prior campaign's own documented
     baseline — this is the concrete proof that adding this second plugin
     did not regress the existing visual smoke test.
   - `stop_app_background` when done.
4. Rebuild and re-run `tools/ci/gte_plugin_isolation_probe` per its own
   existing nested-configure convention (see its `CMakeLists.txt`) and confirm
   its new `PASS:` line reports 4 plugins loaded, 2 implementing
   `IRenderFeatureModule_v1`.

## Completion

Write `PHASE5_COMPLETION_REPORT.md`: what changed, the exact warning log line
observed, the visual confirmation (still solid magenta), the isolation
probe's new pass output. Then `git_add` + `git_commit` (message referencing
PHASE5).
