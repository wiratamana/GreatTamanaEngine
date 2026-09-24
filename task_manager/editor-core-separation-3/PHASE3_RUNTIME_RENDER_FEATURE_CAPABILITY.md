# PHASE3 — `IRenderFeatureModule_v1`: A Plugin Contributes a Real Render-Graph Pass

**Parent**: `PHASE0_MASTER_STRATEGY.md`. **Predecessor**:
`PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md` — `PluginHost` must
already be wired into `Core` and loading real `.dll`s before starting this
phase.

Maps to the source design doc's Milestone 1. **This is the core of this
campaign** — the first genuinely new engine BEHAVIOR a plugin can add.

---

## Step 1: The Goal (Where are we going, this phase specifically?)

A plugin `.dll` in `<build-dir>/plugins/` makes `GreatTamanaEditor.exe`
render one extra, real render-graph pass every frame — with **zero
hardcoded knowledge of that specific plugin anywhere inside `Core`**. By the
end of this phase:

- `IRenderFeatureModule_v1` (new, `gte_plugin_abi`) — the capability a
  plugin implements to contribute per-frame render-graph passes.
- `IPluginRenderPassBuilder` (new, `gte_plugin_abi`) — the deliberately
  tiny, curated WRAPPER interface a plugin uses instead of ever touching a
  real `rg::RenderGraphBuilder` (Locked Design Decision #2,
  `PHASE0_MASTER_STRATEGY.md`).
- `PluginRenderPassBuilderAdapter` (new, `gte_core`) — implements
  `IPluginRenderPassBuilder`, constructed fresh every frame, wrapping the
  real `rg::RenderGraphBuilder&` + this frame's real Game-View target
  handle.
- A new `"PluginRenderFeatures"` provider, registered onto
  `Core::m_offscreenRenderPipeline` inside
  `Core::RegisterOffscreenRenderPipelineProviders()`, that generically loops
  `m_pluginHost.AllLoadedModules()`, queries `IRenderFeatureModule_v1`, and
  calls `AddRenderGraphPasses()` on every module that implements it.
- One throwaway demo plugin, `plugins/demo_render_feature/`, whose ONE
  render pass clears the Game View to a solid, distinctive debug color —
  exactly the source design doc's own Milestone 1 example.
- Confirmed, live, via `gte_send_request("/get_swapchain")`/
  `"/get_game_view")`: the demo plugin's own solid color is genuinely
  visible in the rendered frame.

---

## Step 2: The Situation

- `Core::RegisterOffscreenRenderPipelineProviders()` (`src/Core/Core.cpp`,
  read in full during this campaign's own investigation) genuinely registers
  providers in this real, confirmed sequence: `"AtmosphereSharedLut"` (Once) →
  `"GpuSkinning"` (Once) → `"AtmosphereViewLut"` (PerActiveView) →
  `"RenderOpaque"` (PerActiveView) → `"GpuDrivenBatches"` (PerActiveView) →
  `"DrawSkyBackground"` (PerActiveView) → `"RenderTransparent"` (PerActiveView) →
  `"AtmosphereComposite"` (PerActiveView, `ProviderTiming::AfterDeferredPasses`)
  — see Step 3.4 below for why `"AtmosphereComposite"`'s own non-default
  `timing` argument matters directly to this phase. **This phase's own new
  `"PluginRenderFeatures"` provider must be registered LAST, after every real
  production provider, AND with `ProviderTiming::AfterDeferredPasses` (Step
  3.4)** — a plugin's own pass should never be silently culled/reordered
  relative to production passes it has no way to know about, and rendering
  last (on top of whatever the real frame already drew) is the correct, safe
  default for an unknown, arbitrary future plugin's own pass (mirrors how a
  post-processing/overlay pass conventionally runs last).
- Each existing provider's own lambda signature is
  `[this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out)`.
  `rg::RenderPassFrameContext` (confirm its exact fields via `read_file` on
  `src/Renderer/RenderGraph/RenderPipeline.h`) exposes at least `builder`
  (the real `rg::RenderGraphBuilder&`), `currentView`, and a `blackboard` for
  cross-provider data — `PluginRenderPassBuilderAdapter` wraps exactly this
  `frame` object plus whatever this frame's resolved Game-View render target
  handle is (`Core::FindViewData(frame.currentView)`'s own
  `RenderPassViewData`, mirroring `"RenderOpaque"`'s own real body for how
  to resolve the correct target/aspect ratio for the CURRENT view).
- `ProviderScope::PerActiveView` (used by `"RenderOpaque"`) means this
  provider's own lambda runs ONCE PER currently-visible view (Game View
  and/or Scene View) — this phase's own `"PluginRenderFeatures"` provider
  uses the SAME scope, so a plugin's pass genuinely renders in BOTH Game and
  Scene View when both are visible, exactly mirroring every real production
  pass's own behavior — a plugin should not need to know Game View and Scene
  View exist as a distinct concept at all; the curated wrapper handles that
  distinction entirely on the host side.
- `AddDrawSkyBackgroundPass()`/`AddRenderOpaquePass()`
  (`src/Application/RenderPasses.h`, read in full) are the closest REAL
  precedent for "how do I declare one graphics pass that clears/writes a
  color target" — `PluginRenderPassBuilderAdapter::AddFullscreenClearPass()`
  (Step 3.2 below)'s own real implementation is a small, new function
  mirroring their shape (a `builder.AddRenderPass(...)` call with a `setup`
  lambda declaring one `WriteColorAttachment()` and an `execute` lambda
  issuing the actual clear), not a reuse of either function directly (they
  each carry real, Game/Scene-View-specific, non-reusable logic of their
  own — e.g. GPU-skinning buffer reads, Frame Debugger capture recording —
  that a plugin's own deliberately tiny clear pass must not accidentally
  inherit).

---

## Step 3: The Plan

### 3.1 — `plugins/gte_plugin_abi/IRenderFeatureModule.h`

```cpp
#pragma once

namespace gte {

class IPluginRenderPassBuilder;

// A plugin implements this to contribute render-graph passes every frame -
// source design doc, Section 2/5. Queried via
// IPluginModule::QueryCapability("IRenderFeatureModule_v1").
//
// Called once per currently-visible view (Game View and/or Scene View),
// mirroring every real production RenderPipeline provider's own
// PerActiveView scope (see Core::RegisterOffscreenRenderPipelineProviders(),
// gte_core) - a plugin does not need to know Game View/Scene View exist as
// a distinct concept; `builder` already reflects whichever view is
// currently being declared into.
class IRenderFeatureModule_v1 {
public:
    virtual ~IRenderFeatureModule_v1() = default;

    virtual void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) = 0;
};

inline constexpr const char* kIRenderFeatureModule_v1_Name = "IRenderFeatureModule_v1";

} // namespace gte
```

### 3.2 — `plugins/gte_plugin_abi/IPluginRenderPassBuilder.h`

```cpp
#pragma once

namespace gte {

// The ONLY way a plugin ever declares a render-graph pass - source design
// doc Section 3.3/5's own "small, curated, ABI-contract wrapper... never
// the raw internal type verbatim", made concrete and DELIBERATELY MINIMAL
// for this campaign's own Milestone 1 scope (PHASE0_MASTER_STRATEGY.md,
// Locked Design Decision #2/#3): a plugin NEVER receives a real
// rg::RenderGraphBuilder&, ever. Every method here uses ONLY plain built-in
// types - no std::string/std::vector/gte_core type crosses this boundary.
//
// Deliberately narrow for v1 - exactly enough to satisfy this campaign's
// own throwaway demo ("clears the screen to a solid debug color"). A future
// _v2 (a NEW interface, additive, never redefining this one's meaning -
// source design doc Section 4.3/8) is where a genuinely richer pass-
// building surface (textured draws, compute dispatches, reading another
// pass's output) would be designed, once a REAL future capability actually
// needs it - not invented speculatively here.
class IPluginRenderPassBuilder {
public:
    virtual ~IPluginRenderPassBuilder() = default;

    // Declares one graphics pass that clears the CURRENT view's Game/Scene
    // render target to the given solid RGBA color (each component 0.0-1.0)
    // - runs AFTER every real production pass this frame (see
    // PHASE0/PHASE3's own "PluginRenderFeatures" provider ordering). `debugName`
    // must be a stable, static-duration string literal owned by the CALLER
    // (the plugin) - the adapter implementing this interface never takes
    // ownership of it, never frees it, and does not copy it beyond this
    // one call's own duration (Locked Design Decision #3/#4,
    // PHASE0_MASTER_STRATEGY.md).
    virtual void AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a) = 0;
};

} // namespace gte
```

### 3.3 — `src/Core/Plugins/PluginRenderPassBuilderAdapter.h/.cpp` (new, `gte_core`)

```cpp
// PluginRenderPassBuilderAdapter.h
#pragma once

#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {
namespace rg { class RenderGraphBuilder; }

// The gte_core-side implementation of IPluginRenderPassBuilder - constructed
// FRESH, once per currently-visible view, per frame, by the
// "PluginRenderFeatures" provider (Core.cpp) - never held past the end of
// that provider's own lambda invocation. Forwards AddFullscreenClearPass()
// into a real rg::RenderGraphBuilder::AddRenderPass() call against the
// real target this frame's view actually resolved to - see .cpp.
class PluginRenderPassBuilderAdapter final : public IPluginRenderPassBuilder {
public:
    PluginRenderPassBuilderAdapter(rg::RenderGraphBuilder& builder, rg::TextureHandle viewTarget) noexcept
        : m_builder(builder), m_viewTarget(viewTarget)
    {
    }

    void AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a) override;

private:
    rg::RenderGraphBuilder& m_builder;
    rg::TextureHandle m_viewTarget;
};

} // namespace gte
```

`.cpp` body (mirrors `AddDrawSkyBackgroundPass()`'s own real shape in
`src/Application/RenderPasses.cpp`, and `GBufferValidation.cpp`'s own real
`RenderPassCategory::Debug` call site — both read in full during this
campaign's own investigation, and used here as the confirmed, correct real
call shape):
```cpp
void PluginRenderPassBuilderAdapter::AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a)
{
    // Confirmed against the REAL, current RenderGraphBuilder::AddRenderPass()
    // overload (src/Renderer/RenderGraph/RenderGraphBuilder.h): the shape
    // this file originally sketched (name, setup, execute, kind, category -
    // trailing) does NOT exist. The real, non-defaulted parameter order is
    // (name, kind, viewScope, category, setup, execute[, drawKind,
    // renderPassEvent, tags]), exactly as every real call site
    // (GBufferValidation.cpp, FrameDebuggerReplayPasses.cpp) already uses it.
    // ViewScope::Shared (not Game/SceneView) is used here since this adapter
    // is constructed fresh per-view with that view's own resolved
    // viewData->colorTarget already baked in - the plugin itself never needs
    // to know Game View/Scene View exist as a distinct concept (Step 2).
    m_builder.AddRenderPass(debugName, rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::Debug,
        [this, r, g, b, a](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(m_viewTarget, std::array<float, 4>{ r, g, b, a });
        },
        [](rg::PassContext&) {
            // Intentionally empty - WriteColorAttachment()'s own declared
            // clear color IS the entire visible effect of this pass; no
            // additional draw call is issued.
        });
}
```
`RenderPassCategory::Debug` (not `General`) is the deliberate, correct
category for every plugin-contributed pass — mirrors
`GBufferValidation.cpp`'s own precedent of tagging genuinely optional/
debug-flavored passes this way.

### 3.4 — New provider in `Core::RegisterOffscreenRenderPipelineProviders()`

Real, confirmed order of `m_offscreenRenderPipeline.Register(...)` calls in
`Core::RegisterOffscreenRenderPipelineProviders()` today: `"AtmosphereSharedLut"`
(Once) → `"GpuSkinning"` (Once) → `"AtmosphereViewLut"` (PerActiveView) →
`"RenderOpaque"` (PerActiveView) → `"GpuDrivenBatches"` (PerActiveView) →
`"DrawSkyBackground"` (PerActiveView) → `"RenderTransparent"` (PerActiveView) →
`"AtmosphereComposite"` (PerActiveView, **`rg::ProviderTiming::AfterDeferredPasses`**)
— `"AtmosphereComposite"` is genuinely the LAST provider registered today, and
it uses a non-default `timing` argument for a real, load-bearing reason (see
`RenderPipeline.h`'s own `ProviderTiming` doc comment): `RenderPipeline::
DeclareInto()` runs in TWO SEPARATE phases — every `BeforeDeferredPasses`
provider first (the default every other provider above uses, including
`"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"`), each phase's own
deferred `RenderPassDesc` list sorted+flushed BEFORE the next phase's
providers even run — so plain REGISTRATION ORDER alone does NOT guarantee
"runs last"; only `ProviderTiming::AfterDeferredPasses` does, by placing a
provider in the SECOND phase, strictly after every `BeforeDeferredPasses`
provider's own passes (deferred or immediate) have already been declared.

This matters directly here because `PluginRenderPassBuilderAdapter::
AddFullscreenClearPass()` calls `frame.builder.AddRenderPass()` IMMEDIATELY —
the same "immediate" style `"AtmosphereComposite"` itself uses, NOT the
deferred `out.push_back()` style `"RenderOpaque"`/`"DrawSkyBackground"`/
`"RenderTransparent"` use. Per `ProviderTiming`'s own doc comment, an immediate
call always lands in the underlying pass list BEFORE any deferred provider's
own passes declared in THAT SAME PHASE, regardless of `RenderPassEvent`. If
`"PluginRenderFeatures"` were registered with the default
`ProviderTiming::BeforeDeferredPasses`, its immediate clear pass would
actually be declared BEFORE `"RenderOpaque"`/`"DrawSkyBackground"`/
`"RenderTransparent"` even run — the exact opposite of "runs last, clears on
top of everything" this phase intends, and exactly the class of bug
`RenderPipeline.h`'s own `ProviderTiming` doc comment already documents
discovering and fixing for `"AtmosphereComposite"` itself (a real,
live-testing-confirmed correctness bug in that phase's own history).
**`"PluginRenderFeatures"` must therefore be registered with
`rg::ProviderTiming::AfterDeferredPasses` explicitly (the same as
`"AtmosphereComposite"`), as the LAST `Register(...)` call in the function,
immediately after `"AtmosphereComposite"`'s own call** — placing it in the
same second phase, after `"AtmosphereComposite"` in registration order within
that phase:

```cpp
// PHASE3 (editor-core-separation-3 campaign) - the ONE generic,
// capability-agnostic loop that lets any loaded plugin .dll contribute a
// render-graph pass, with ZERO hardcoded knowledge of any specific plugin
// here (source design doc, Section 5). Registered with
// ProviderTiming::AfterDeferredPasses (NOT the default) - this provider
// calls frame.builder.AddRenderPass() immediately, so only this timing
// guarantees it truly runs after every deferred production pass
// ("RenderOpaque"/"DrawSkyBackground"/"RenderTransparent") AND after
// "AtmosphereComposite"'s own immediate call - see this phase's own Step 3.4
// write-up for the full reasoning.
m_offscreenRenderPipeline.Register("PluginRenderFeatures", rg::ProviderScope::PerActiveView,
    [this](const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&) {
        const RenderPassViewData* viewData = FindViewData(frame.currentView);
        if (viewData == nullptr) {
            return;
        }
        for (IPluginModule* module : m_pluginHost.AllLoadedModules()) {
            if (auto* feature = static_cast<IRenderFeatureModule_v1*>(
                    module->QueryCapability(kIRenderFeatureModule_v1_Name))) {
                PluginRenderPassBuilderAdapter adapter(frame.builder, viewData->colorTarget);
                feature->AddRenderGraphPasses(adapter);
            }
        }
    },
    rg::ProviderTiming::AfterDeferredPasses);
```

`RenderPassViewData`'s real, confirmed field name for "this view's own render
target handle" is `colorTarget` (`src/Application/RenderPassViewData.h`) — NOT
`target` as an earlier, necessarily-approximate sketch of this file assumed;
every real production provider (`"RenderOpaque"`/`"GpuDrivenBatches"`/
`"DrawSkyBackground"`/`"RenderTransparent"`/`"AtmosphereComposite"`) already
reads `viewData->colorTarget` the same way, confirmed directly in `Core.cpp`.

`#include`s needed in `Core.cpp`: the two new `gte_plugin_abi` headers
(`IRenderFeatureModule.h`, `IPluginRenderPassBuilder.h` — likely already
transitively available via `Core.h`'s own new `PluginHost.h` include from
PHASE2, but confirm and add explicitly if not) and
`"Plugins/PluginRenderPassBuilderAdapter.h"`.

### 3.5 — Demo plugin: `plugins/demo_render_feature/`

Mirrors `plugins/demo_hello_world/`'s own CMake shape exactly (Step 3.5 of
PHASE2), a new sibling target `demo_render_feature`, staged into the same
`<build-dir>/plugins/` output folder.

`plugins/demo_render_feature/RenderFeaturePlugin.cpp`:
```cpp
#include "../gte_plugin_abi/IPluginModule.h"
#include "../gte_plugin_abi/GtePluginModuleInfo.h"
#include "../gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder.h"

#include <cstring>

namespace gte {
namespace {

class DemoRenderFeature final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) override
    {
        // A distinctive, unmistakable magenta - never a color any real
        // production pass in this engine uses today (confirmed via
        // search_in_dir on every existing clear-color constant before
        // picking this) - so this phase's own visual smoke test can never
        // be confused with a real rendering bug or a pre-existing pass.
        builder.AddFullscreenClearPass("DemoRenderFeaturePlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f);
    }
};

class DemoRenderFeaturePluginModule final : public IPluginModule {
public:
    void* QueryCapability(const char* nameAndVersion) override
    {
        if (std::strcmp(nameAndVersion, kIRenderFeatureModule_v1_Name) == 0) {
            return static_cast<IRenderFeatureModule_v1*>(&m_feature);
        }
        return nullptr;
    }
    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override
    {
        std::strncpy(outInfo.name, "DemoRenderFeaturePlugin", sizeof(outInfo.name) - 1);
        std::strncpy(outInfo.version, "1.0.0", sizeof(outInfo.version) - 1);
        std::strncpy(outInfo.description, "Milestone 1 proof - clears the Game/Scene View to solid magenta.", sizeof(outInfo.description) - 1);
    }

private:
    DemoRenderFeature m_feature;
};

} // namespace
} // namespace gte

extern "C" {
__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint() { return gte::MakeThisBuildsFingerprint(); }
__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return new gte::DemoRenderFeaturePluginModule(); }
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module) { delete module; }
}
```

---

## Step 4: Verification (this phase's own mandatory checkpoint)

1. Incremental compile: `gte_plugin_abi` → `demo_render_feature` →
   `gte_core` → `gte_editor` → `GreatTamanaEditor`.
2. `run_app_background`; `gte_send_request("/activate_tab?name=Game")` (the
   Game tab must be the active dock tab for the Game View to actually
   render this frame — mirrors `editor-core-separation-2`'s own documented
   "Investigation finding" precedent exactly, avoid repeating that
   confusion); `gte_send_request("/get_game_view")`. **Confirm the returned
   image is genuinely, visibly solid magenta** — use `load_image`/direct
   visual inspection of the returned image content, not just "the HTTP
   status was 200."
3. `gte_send_request("/get_logs?limit=20")` — confirm both
   `HelloWorldPlugin` and `DemoRenderFeaturePlugin` are logged as loaded.
4. Confirm the Scene View ALSO shows the same magenta clear (activate the
   Scene tab, re-capture) — proving `ProviderScope::PerActiveView` genuinely
   ran this pass for both views, per Step 2's own stated design intent.
5. `stop_app_background`.

---

## Universal Rules

Follow `PHASE0_MASTER_STRATEGY.md`'s Universal Rules exactly.

## Non-Goals for this phase specifically

- No editor-tier capability yet (PHASE4).
- No richer `IPluginRenderPassBuilder` surface (textured draws, compute,
  cross-pass reads) — deliberately deferred to a future `_v2`, not invented
  here ahead of a real need.
