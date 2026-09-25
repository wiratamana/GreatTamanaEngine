// plugins/demo_render_feature_v3_second/RenderFeaturePlugin.cpp
//
// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.5) - a real, PERMANENT
// _v3 demo plugin: a BLUE radial vignette, centered, opaque core fading to
// fully transparent by the frame's edge, stage PreUI, priority 0, blend mode
// AlphaOver - reimplements demo_render_feature_v2_second's OWN exact effect
// (same center/radii/color) via the new, generic
// Dispatch("gte.builtin.radial_vignette", ...) mechanism, for a direct, real,
// live pixel-parity A/B proof (see PHASE2_COMPLETION_REPORT.md for exactly
// how the two are compared - one at a time, via the already-existing
// GET /render_graph/set_feature_enabled toggle, since two AlphaOver vignettes
// stacked on top of each other would NOT look the same as one alone).

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/RenderFeatureDescriptor.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder_v3.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

// Plugin-side mirror of gte_core's own RenderFeatureOpsPushConstants - see
// demo_render_feature_v3's own identical struct/doc comment for the full
// "why".
struct DemoV3OpsPushConstants {
    float opCodeAndPad[4] = {};
    float colorRgba[4] = {};
    float centerAndRadius[4] = {};
    float gradeParams[4] = {};
};

// See demo_render_feature_v3's own identical FillPassState/ring-buffer doc
// comment for exactly why this exists (this plugin's own AddRenderGraphPasses()
// may likewise be called twice in one frame, once per active view, before
// either call's own `execute` callback actually runs).
constexpr int kMaxInFlightPassStates = 4;

struct VignettePassState {
    PluginTextureHandle target;
};

VignettePassState g_vignetteStates[kMaxInFlightPassStates];
int g_vignetteStateCounter = 0;

void VignetteSetup(IPluginPassSetupContext& ctx, void* userData)
{
    auto* state = static_cast<VignettePassState*>(userData);
    ctx.WriteTexture(state->target, PluginResourceAccess::ComputeShaderWrite);
}

void VignetteExecute(IPluginCommandRecorder& recorder, void* userData)
{
    auto* state = static_cast<VignettePassState*>(userData);

    // Byte-for-byte identical parameters to demo_render_feature_v2_second's
    // own "DemoRenderFeatureV2Second_Vignette" (RenderFeaturePlugin.cpp,
    // plugins/demo_render_feature_v2_second/): centerX=0.5, centerY=0.5,
    // innerRadius=0.15, outerRadius=0.65, color=BLUE opaque.
    DemoV3OpsPushConstants pushConstants;
    pushConstants.colorRgba[0] = 0.0f;
    pushConstants.colorRgba[1] = 0.0f;
    pushConstants.colorRgba[2] = 1.0f;
    pushConstants.colorRgba[3] = 1.0f;
    pushConstants.centerAndRadius[0] = 0.5f;
    pushConstants.centerAndRadius[1] = 0.5f;
    pushConstants.centerAndRadius[2] = 0.15f;
    pushConstants.centerAndRadius[3] = 0.65f;

    recorder.BindTexture(0, state->target);
    // See demo_render_feature_v3's own identical doc comment for exactly why
    // the maximum allowed group count is dispatched unconditionally.
    recorder.Dispatch("gte.builtin.radial_vignette", &pushConstants, sizeof(pushConstants),
        kPluginComputeDispatchMaxGroupsPerDimension, kPluginComputeDispatchMaxGroupsPerDimension, 1);
}

class DemoRenderFeatureV3Second final : public IRenderFeatureModule_v3 {
public:
    GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const override
    {
        return MakeRenderFeatureDescriptor("DemoRenderFeatureV3Second",
            RenderFeatureStage::PreUI, /*priority=*/0, RenderFeatureBlendMode::AlphaOver);
    }

    void AddRenderGraphPasses(IPluginRenderPassBuilder_v3& builder) override
    {
        VignettePassState& state = g_vignetteStates[g_vignetteStateCounter % kMaxInFlightPassStates];
        ++g_vignetteStateCounter;
        state.target = builder.GetPrivateOutputTarget();
        builder.AddComputePass("DemoRenderFeatureV3Second_Vignette", &VignetteSetup, &VignetteExecute, &state);
    }
};

DemoRenderFeatureV3Second g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v3> g_module(
    g_feature, kIRenderFeatureModule_v3_Name,
    MakeModuleInfo("DemoRenderFeatureV3SecondPlugin", "1.0.0",
        "editor-core-separation-9 PHASE2 proof - reimplements demo_render_feature_v2_second's exact BLUE radial "
        "vignette via the new generic Dispatch(\"gte.builtin.radial_vignette\", ...) mechanism, for a direct "
        "pixel-parity A/B comparison (see PHASE2_COMPLETION_REPORT.md)."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
