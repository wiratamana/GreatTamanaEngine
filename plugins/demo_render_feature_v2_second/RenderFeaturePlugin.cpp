// plugins/demo_render_feature_v2_second/RenderFeaturePlugin.cpp
//
// editor-core-separation-6 campaign, PHASE6
// (PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md) - a real, PERMANENT, SECOND,
// genuinely independent _v2 demo plugin: a BLUE radial vignette, centered,
// opaque core fading to fully transparent by the frame's edge - genuinely
// distinguishable from the first plugin's own flat, uniform RED fill both
// in COLOR and in SPATIAL pattern (not just a different flat color), stage
// PreUI, priority 0, blendMode AlphaOver - proving real multi-plugin
// compositing, not the same-color trick the original _v1 demos used (see
// RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_2026-09-25.md Part 1.5).

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/RenderFeatureDescriptor.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder_v2.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

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
