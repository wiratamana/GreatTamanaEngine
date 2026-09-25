#pragma once

#include "RenderFeatureDescriptor.h"

// editor-core-separation-3 campaign, PHASE3
// (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md) - maps to the source design
// doc's Milestone 1: the capability a plugin implements to contribute
// per-frame render-graph passes.

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

class IPluginRenderPassBuilder_v2;

// editor-core-separation-6 campaign, PHASE1
// (PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md) - ADDITIVE new interface,
// _v1 (above) completely untouched. See RenderFeatureDescriptor.h and
// IPluginRenderPassBuilder_v2.h.
class IRenderFeatureModule_v2 {
public:
    virtual ~IRenderFeatureModule_v2() = default;

    // Called exactly once, right after this plugin loads (RenderFeatureCompositor::
    // OnPluginsLoaded(), PHASE4) - the returned descriptor is snapshotted and
    // reused for this plugin's entire loaded lifetime; this method is never
    // called again afterward (mirrors the Proposal's own Section 3.4 step 1).
    virtual GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const = 0;

    // Called once per frame, per active view (Game View and/or Scene View),
    // ONLY while this plugin's own declared stage is one RenderFeatureCompositor
    // actually processes this frame - mirrors IRenderFeatureModule_v1::
    // AddRenderGraphPasses()'s own "a plugin does not need to know Game
    // View/Scene View exist as a distinct concept" contract exactly.
    // `builder` targets THIS PLUGIN'S OWN PRIVATE offscreen target for this
    // call - never a target shared with any other loaded plugin (PHASE0_
    // MASTER_STRATEGY.md Locked Design Decision, the single most important
    // structural difference vs. _v1).
    virtual void AddRenderGraphPasses(IPluginRenderPassBuilder_v2& builder) = 0;
};

inline constexpr const char* kIRenderFeatureModule_v2_Name = "IRenderFeatureModule_v2";

class IPluginRenderPassBuilder_v3;

// editor-core-separation-9 campaign, PHASE1
// (PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md) - ADDITIVE new
// interface, _v1/_v2 (above) completely untouched. See PluginRenderResource.h
// and IPluginRenderPassBuilder_v3.h. This is the RECOMMENDED path for new
// plugin authors going forward (PHASE0_MASTER_STRATEGY.md Locked Product
// Decision #1) - _v2 remains fully supported, forever, for backward
// compatibility. Forward-declares IPluginRenderPassBuilder_v3 rather than
// #include-ing IPluginRenderPassBuilder_v3.h, mirroring how this same file
// already forward-declares IPluginRenderPassBuilder_v2 for _v2's own
// identical reason: keep this header light for anything that only needs
// IRenderFeatureModule_v1.
class IRenderFeatureModule_v3 {
public:
    virtual ~IRenderFeatureModule_v3() = default;

    // Called exactly once, right after this plugin loads - identical
    // contract to IRenderFeatureModule_v2::GetRenderFeatureDescriptor()
    // above (same GtePluginRenderFeatureDescriptor struct, unchanged).
    virtual GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const = 0;

    // Called once per frame, per active view, ONLY while this plugin's own
    // declared stage is one RenderFeatureCompositor actually processes this
    // frame - identical calling contract to IRenderFeatureModule_v2::
    // AddRenderGraphPasses() above, just with the new, generic _v3 builder.
    virtual void AddRenderGraphPasses(IPluginRenderPassBuilder_v3& builder) = 0;
};

inline constexpr const char* kIRenderFeatureModule_v3_Name = "IRenderFeatureModule_v3";

} // namespace gte
