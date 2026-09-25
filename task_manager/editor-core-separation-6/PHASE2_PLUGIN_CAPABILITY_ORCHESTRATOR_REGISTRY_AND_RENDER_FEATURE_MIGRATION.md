# PHASE2 — `IPluginCapabilityOrchestrator` Registry + Legacy Render-Feature Migration

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST). Also read
`PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md`'s own completion report for
continuity before starting.

## Step 1: The Goal

Introduce the ONE generic, reusable mechanism
(`RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md` Section
3.5) that lets `Core` discover/react to loaded plugin capabilities WITHOUT a
bespoke, hand-written `for` loop per capability kind hardcoded inline inside
already-large methods. Prove it works by migrating the EXISTING
`IRenderFeatureModule_v1` loop onto it FIRST, with **zero observable
behavior change** — same warning text, same pass names, same everything.
This phase does not add any new user-visible capability; it is a pure,
verifiable internal refactor.

## Step 2: The Situation

Confirmed by direct read, `src/Core/Core.cpp`:

- `Core::LoadPlugins()` (lines ~227-249): calls
  `m_pluginHost.LoadPlugins(pluginsDirectory)`, then immediately calls
  `CountModulesImplementingRenderFeature(m_pluginHost.AllLoadedModules())`
  and logs `GTE_LOG_WARNING("PluginHost", ...)` if the count is `> 1`. Exact
  current warning text (byte-for-byte, must survive this phase unchanged):

  ```
  "<N> loaded plugins implement IRenderFeatureModule_v1 - only the
  LAST-registered one's render output will be visible this frame (render-graph
  compositing for multiple render-feature plugins is not implemented - see
  docs/conventions/plugin-architecture.md)."
  ```

- `Core::RegisterOffscreenRenderPipelineProviders()`'s `"PluginRenderFeatures"`
  provider (lines ~750-775, the LAST `Register(...)` call in that method,
  `rg::ProviderScope::PerActiveView`, `rg::ProviderTiming::AfterDeferredPasses`):

  ```cpp
  m_offscreenRenderPipeline.Register("PluginRenderFeatures", rg::ProviderScope::PerActiveView,
      [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
          const RenderPassViewData* viewData = FindViewData(frame.currentView);
          if (viewData == nullptr) { return; }
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

  Note the lambda's own second parameter, `std::vector<rg::RenderPassDesc>&`
  (currently unused/unnamed) — this is EXACTLY the same shape the Proposal's
  own `IPluginCapabilityOrchestrator::ContributeRenderGraphPasses(const
  rg::RenderPassFrameContext&, std::vector<rg::RenderPassDesc>&)` uses. No
  provider-registration-callback signature change is needed anywhere.

- `src/Core/Plugins/PluginRenderFeatureDiagnostics.h`/`.cpp` —
  `CountModulesImplementingRenderFeature()`, a small free function.

## Step 3: The Plan

### Step 3.1 — New file: `src/Core/Plugins/IPluginCapabilityOrchestrator.h`

```cpp
#pragma once

#include <vector>

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md Section
// 3.5: the ONE generic, reusable interface for "when plugins finish
// loading, let every interested HOST subsystem discover and react to them" -
// replacing a bespoke, hand-written for-loop per capability kind
// (IRenderFeatureModule_v1's loop lived inline in Core::LoadPlugins()/
// Core::RegisterOffscreenRenderPipelineProviders(); IEditorPanelModule_v1's
// loop lived inline in EditorHost.cpp - see PHASE3 for that migration).
// This header lives in gte_core (never gte_plugin_abi - it is HOST-internal
// wiring, never crosses the plugin ABI boundary, so it is free to use
// std::vector/real gte_core types like rg::RenderPassFrameContext).

namespace gte {

class IPluginModule;

namespace rg {
struct RenderPassFrameContext;
struct RenderPassDesc;
} // namespace rg

// One instance per DISTINCT plugin capability kind Core cares about (render
// features today; editor panels, PHASE3; any future kind later). Core owns
// a plain std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>>,
// populated ONCE, at construction time, by
// Core::RegisterBuiltinCapabilityOrchestrators() - adding a brand-new
// capability kind in the future means writing ONE new class implementing
// this interface and adding ONE line to that registration function, never
// touching Core::LoadPlugins()'s own body, never touching
// Core::RegisterOffscreenRenderPipelineProviders()'s own body, ever again.
class IPluginCapabilityOrchestrator {
public:
    virtual ~IPluginCapabilityOrchestrator() = default;

    // Called once, right after PluginHost::LoadPlugins() returns
    // (Core::LoadPlugins(), below). Implementations discover which loaded
    // modules answer their own capability's QueryCapability() name,
    // validate/order them (e.g. RenderFeatureCompositor's own
    // priority-collision detection, PHASE4), and log anything worth
    // knowing. Never draws/renders here - discovery and validation only.
    virtual void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) = 0;

    // Optional hook - only orchestrators that actually affect the render
    // graph override this (RenderFeatureCompositor does, PHASE4/PHASE5; a
    // future EditorPanelCapabilityOrchestrator, PHASE3, does not need to,
    // since panels are drawn through Dear ImGui, not the render graph).
    // Default no-op. Signature matches "PluginRenderFeatures" provider's
    // own existing lambda parameters EXACTLY (Step 2 above) - no provider-
    // registration change needed anywhere.
    virtual void ContributeRenderGraphPasses(const rg::RenderPassFrameContext& /*frame*/,
        std::vector<rg::RenderPassDesc>& /*out*/)
    {
    }
};

} // namespace gte
```

### Step 3.2 — New files: `src/Core/Plugins/LegacyRenderFeatureOrchestrator.h` + `.cpp`

This class's ENTIRE body is a verbatim RELOCATION of the two code blocks
quoted in Step 2 above — every string literal, every variable name, every
control-flow branch stays byte-for-byte identical. Only the "home" of the
code changes (from inline lambdas/`Core::LoadPlugins()` to this class's own
two methods).

`LegacyRenderFeatureOrchestrator.h`:

```cpp
#pragma once

#include "IPluginCapabilityOrchestrator.h"

namespace gte {

class Core; // forward declaration only - this header must not #include "../Core.h"
            // (that would be a circular include: Core.h itself will gain a member
            // of type std::unique_ptr<IPluginCapabilityOrchestrator>, PHASE2 Step 3.4,
            // and LegacyRenderFeatureOrchestrator.h is reachable from Core.cpp's own
            // #include list). The .cpp file #includes "../Core.h" for the real
            // Core& method calls (Core::FindPluginRenderFeatureTarget()/GetPluginHost()).

// editor-core-separation-6 campaign, PHASE2
// (PHASE2_PLUGIN_CAPABILITY_ORCHESTRATOR_REGISTRY_AND_RENDER_FEATURE_MIGRATION.md)
// - a VERBATIM relocation of Core::LoadPlugins()'s own multi-plugin warning
// (editor-core-separation-4, PHASE5) and Core::RegisterOffscreenRenderPipelineProviders()'s
// own "PluginRenderFeatures" IRenderFeatureModule_v1 loop
// (editor-core-separation-3, PHASE3) into the new IPluginCapabilityOrchestrator
// shape - ZERO observable behavior change (same warning text, same pass
// names). This is the PERMANENT home for the _v1 "last write wins, shared
// handle" legacy path (PHASE0_MASTER_STRATEGY.md Locked Design Decision #9)
// - _v1 plugins never migrate onto RenderFeatureCompositor (PHASE4/PHASE5).
class LegacyRenderFeatureOrchestrator final : public IPluginCapabilityOrchestrator {
public:
    // `core` is the SAME Core instance constructing this orchestrator inside
    // Core::RegisterBuiltinCapabilityOrchestrators() (Step 3.4 below,
    // `std::make_unique<LegacyRenderFeatureOrchestrator>(*this)`) - stored as a
    // reference, never a pointer, since it must outlive this orchestrator for
    // Core's entire remaining lifetime (this orchestrator lives inside
    // Core::m_capabilityOrchestrators, a member of that same Core instance).
    explicit LegacyRenderFeatureOrchestrator(Core& core) noexcept : m_core(core) { }

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

private:
    Core& m_core;
};

} // namespace gte
```

`LegacyRenderFeatureOrchestrator.cpp` — `OnPluginsLoaded()` body is EXACTLY
the current `Core::LoadPlugins()` body from
`const int renderFeatureModuleCount = ...` through the closing brace of the
`if` block (Step 2's quoted warning text, unchanged). `ContributeRenderGraphPasses()`
resolves the view's plugin-facing target via the new shared
`Core::FindPluginRenderFeatureTarget()` accessor (Step 3.3 below) instead of
re-deriving `isGameView`/`compositedKey`/`pluginTarget` inline, then keeps
the rest of the original loop body (`QueryCapability`/`PluginRenderPassBuilderAdapter`/
`finalTextureOutputs.push_back`) otherwise unchanged:

```cpp
void LegacyRenderFeatureOrchestrator::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&)
{
    const std::optional<Core::PluginRenderFeatureTargetInfo> resolved = m_core.FindPluginRenderFeatureTarget(frame);
    if (!resolved.has_value()) {
        return;
    }
    bool anyPluginFeatureRanThisView = false;
    for (IPluginModule* module : m_core.GetPluginHost().AllLoadedModules()) {
        if (auto* feature = static_cast<IRenderFeatureModule_v1*>(
                module->QueryCapability(kIRenderFeatureModule_v1_Name))) {
            PluginRenderPassBuilderAdapter adapter(frame.builder, resolved->target);
            feature->AddRenderGraphPasses(adapter);
            anyPluginFeatureRanThisView = true;
        }
    }
    if (anyPluginFeatureRanThisView) {
        frame.finalTextureOutputs.push_back(resolved->target);
    }
}
```

The computed value of `pluginTarget`/`resolved->target` is exactly the
same 3 lines `Core.cpp`'s own current code already computes today — they
simply live inside `Core::FindPluginRenderFeatureTarget()` now (Step 3.3)
instead of being re-derived inline here, so this is a zero-observable-
behavior-change relocation, same as the rest of this class. This file
needs `IRenderFeatureModule.h`, `IPluginRenderPassBuilder.h`,
`PluginRenderPassBuilderAdapter.h`, `PluginRenderFeatureDiagnostics.h`
(for `OnPluginsLoaded()`'s own warning logic), `Logging.h`, and `Core.h`
(for `Core&`/`Core::FindPluginRenderFeatureTarget()`/`Core::GetPluginHost()`).

### Step 3.3 — Minimal `Core.cpp`/`Core.h` surface needed by `LegacyRenderFeatureOrchestrator.cpp`

`FindViewData()`, `kGameCompositedOutputKey`, `kSceneCompositedOutputKey` are
currently `Core.cpp`-private (anonymous namespace / private method) —
confirmed by direct read: `Core::FindViewData(rg::RenderViewId) const
noexcept` is declared at `Core.h` line 287, which is AFTER the `private:`
label at line 245 — it IS a private method. `LegacyRenderFeatureOrchestrator`
(and, later, `RenderFeatureCompositor`, PHASE4 — which additionally needs
the view's current pixel extent, not just its target handle) both need
this same resolution logic. **Do NOT add a `friend class
LegacyRenderFeatureOrchestrator;` declaration** — the correct, lowest-risk
fix is one small, NEW, narrow, public `Core`-owned accessor method that
calls the private `FindViewData()` internally and returns `std::nullopt`
when the view isn't found, so no caller outside `Core.cpp` ever needs
`FindViewData()` directly:

**`PluginRenderFeatureTargetInfo` is a PUBLIC NESTED type of `class Core`
itself** (never a free-standing `namespace gte` struct) — this is what makes
`Core::PluginRenderFeatureTargetInfo` (the exact qualified spelling both this
file's own `LegacyRenderFeatureOrchestrator.cpp` snippet above and PHASE4's
`RenderFeatureCompositor` use) the correct, valid spelling; add both members
below inside `class Core { public: ... };`'s own existing public section,
placed right alongside `GetPluginHost()`:

```cpp
// Core.h, inside "class Core { public: ... };"
struct PluginRenderFeatureTargetInfo {
    rg::TextureHandle target;
    VkExtent2D extent{};
};
std::optional<PluginRenderFeatureTargetInfo> FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame) const;
```

```cpp
// Core.cpp - ENTIRE body is exactly the 3 lines that already compute
// isGameView/compositedKey/pluginTarget today, plus the view's extent
// (needed by RenderFeatureCompositor, PHASE4 - LegacyRenderFeatureOrchestrator
// itself simply never reads `.extent`).
std::optional<Core::PluginRenderFeatureTargetInfo> Core::FindPluginRenderFeatureTarget(
    const rg::RenderPassFrameContext& frame) const
{
    const RenderPassViewData* viewData = FindViewData(frame.currentView);
    if (viewData == nullptr) {
        return std::nullopt;
    }
    const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
    const rg::RenderPassId compositedKey = isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey;
    PluginRenderFeatureTargetInfo info;
    info.target = frame.blackboard.Fetch<rg::TextureHandle>(compositedKey).value_or(viewData->colorTarget);
    info.extent = viewData->renderTexture != nullptr ? viewData->renderTexture->Extent() : VkExtent2D{};
    return info;
}
```

This keeps `kGameCompositedOutputKey`/`kSceneCompositedOutputKey` exactly
where they already are (no risk of a duplicate/out-of-sync copy of those
two `rg::RenderPassId` constants), and gives every current and future
consumer (`LegacyRenderFeatureOrchestrator` here; `RenderFeatureCompositor`,
PHASE4) ONE shared, single-source-of-truth way to resolve "this view's
current plugin-facing target," instead of independently-hand-written
copies of the exact same logic. `RenderFeatureCompositor` (PHASE4) is what
actually needs `.extent` — this accessor's return shape is designed for
both consumers from the start, so PHASE4 never needs a second, parallel
"give me this view's pixel extent" accessor of its own.

### Step 3.4 — `Core.h`/`Core.cpp` wiring

`Core.h`: add
`#include <memory>` (if not already present) and a forward declaration for
`IPluginCapabilityOrchestrator`; add a private member
`std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>> m_capabilityOrchestrators;`
and a private method `void RegisterBuiltinCapabilityOrchestrators();`.

`Core.cpp` constructor: call `RegisterBuiltinCapabilityOrchestrators();`
BEFORE `RegisterOffscreenRenderPipelineProviders();` (order matters: the
provider's lambda captures `this` and reads `m_capabilityOrchestrators` at
CALL time every frame, not at registration time, so the exact construction
order between these two calls is not actually load-bearing for correctness
— but placing orchestrator registration first is the clearer, more readable
convention and costs nothing).

```cpp
void Core::RegisterBuiltinCapabilityOrchestrators()
{
    m_capabilityOrchestrators.push_back(std::make_unique<LegacyRenderFeatureOrchestrator>(*this));
}

void Core::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    m_pluginHost.LoadPlugins(pluginsDirectory);
    for (auto& orchestrator : m_capabilityOrchestrators) {
        orchestrator->OnPluginsLoaded(m_pluginHost.AllLoadedModules());
    }
}
```

The `"PluginRenderFeatures"` provider body in
`RegisterOffscreenRenderPipelineProviders()` collapses to:

```cpp
m_offscreenRenderPipeline.Register("PluginRenderFeatures", rg::ProviderScope::PerActiveView,
    [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) {
        for (auto& orchestrator : m_capabilityOrchestrators) {
            orchestrator->ContributeRenderGraphPasses(frame, out);
        }
    },
    rg::ProviderTiming::AfterDeferredPasses);
```

Delete the now-dead `CountModulesImplementingRenderFeature`-related
`#include "Plugins/PluginRenderFeatureDiagnostics.h"` from `Core.cpp` ONLY IF
nothing else in `Core.cpp` still uses it after this move (confirm via
`search_in_dir` before removing any `#include` — do not guess).

### Step 3.5 — CMake

Add `src/Core/Plugins/IPluginCapabilityOrchestrator.h`,
`LegacyRenderFeatureOrchestrator.h`, `LegacyRenderFeatureOrchestrator.cpp` to
`gte_core`'s own source-file list in the root `CMakeLists.txt`, mirroring
exactly how `PluginRenderPassBuilderAdapter.h`/`.cpp` and
`PluginRenderFeatureDiagnostics.h`/`.cpp` are already listed there (find
their exact list entries via `search_in_dir` before editing, and insert the
3 new files immediately alongside them, same relative style).

### Verification

1. Incremental build: `cmake --build build` (Ninja only recompiles the
   changed/new `gte_core` translation units + relinks `GreatTamanaEditor.exe`).
2. Live smoke test: `run_app_background` on `build\GreatTamanaEditor.exe`,
   `POST /clear_logs`, wait ~1-2 seconds for startup to finish, then
   `GET /get_logs?limit=100` — confirm the EXACT SAME
   `"2 loaded plugins implement IRenderFeatureModule_v1 - only the
   LAST-registered one's render output will be visible this frame..."`
   warning text still appears (both `demo_render_feature`/
   `demo_render_feature_second` are still the only 2 `_v1` plugins loaded —
   this phase adds no new plugin). `GET /get_game_view` — confirm the
   Game View is still solid magenta (the exact same pre-existing visual
   baseline). `stop_app_background` the PID afterward.
3. `git_status` — confirm the diff touches only `Core.h`, `Core.cpp`,
   `CMakeLists.txt`, and the 3 new `src/Core/Plugins/` files.

### What this phase does NOT do

- Does not migrate `IEditorPanelModule_v1` (PHASE3).
- Does not add `RenderFeatureCompositor` or anything `_v2`-related (PHASE4).
- Does not change `CountModulesImplementingRenderFeature()`'s own warning
  text, wording, or trigger condition (`> 1` modules) in any way.

### Completion

Write `PHASE2_COMPLETION_REPORT.md` (exact `GET /get_logs` response body
confirming byte-identical warning text, exact `GET /get_game_view` visual
confirmation), then `git_add` + `git_commit`.
