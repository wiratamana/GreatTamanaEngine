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
