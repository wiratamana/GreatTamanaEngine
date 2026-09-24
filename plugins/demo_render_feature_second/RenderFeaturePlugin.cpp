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
