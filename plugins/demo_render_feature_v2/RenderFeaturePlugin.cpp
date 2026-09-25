// plugins/demo_render_feature_v2/RenderFeaturePlugin.cpp
//
// editor-core-separation-6 campaign, PHASE6
// (PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md) - a real, DISTINCT-from-the-
// other-_v2-demo-plugin proof: an opaque RED solid fill, stage
// PostComposite, priority 0, blendMode Replace - deliberately NOT magenta
// (this repo's existing _v1 baseline color) and NOT the same color/blend
// mode as demo_render_feature_v2_second, so this campaign never repeats
// the exact mistake RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_
// 2026-09-25.md Part 1.5 documents.

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/RenderFeatureDescriptor.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder_v2.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

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
