#pragma once

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

} // namespace gte
