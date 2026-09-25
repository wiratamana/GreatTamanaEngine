// plugins/demo_render_feature_v3/RenderFeaturePlugin.cpp
//
// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.5) - a real, PERMANENT
// _v3 demo plugin: an opaque RED solid fill, stage PostComposite, priority 0,
// blend mode Replace - reimplements demo_render_feature_v2's OWN exact effect
// (same color) via the new, generic Dispatch("gte.builtin.solid_fill", ...)
// mechanism, for a direct, real, live pixel-parity A/B proof (see this
// campaign's own PHASE2_COMPLETION_REPORT.md for exactly how the two are
// compared - via the already-existing GET /render_graph/set_feature_enabled
// toggle, one at a time, never simultaneously - two independent Replace-mode
// solid fills of the identical color would still look the same either way,
// but a genuinely ISOLATED A/B comparison is the more rigorous proof).
//
// This is also the PRIMARY _v3 demo plugin PHASE3 will ADD its own 2-pass GPU
// blur demo passes to (PHASE0_MASTER_STRATEGY.md's Locked Product Decision
// #7) - never a second, separate folder for that.

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/RenderFeatureDescriptor.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder_v3.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

// Plugin-side mirror of gte_core's own RenderFeatureOpsPushConstants
// (src/Core/Plugins/PluginRenderOperationRegistry.h) - byte-for-byte
// identical layout, 64 bytes total. The FIRST vec4's .x (opCodeAndPad[0]) is
// IGNORED by the adapter - it is silently overwritten with this op's own
// registered opCode before dispatch - a plugin author using
// gte.builtin.solid_fill/radial_vignette/color_grade never sets this field
// meaningfully and never needs to know it exists; leave it zero-initialized.
struct DemoV3OpsPushConstants {
    float opCodeAndPad[4] = {};
    float colorRgba[4] = {};
    float centerAndRadius[4] = {};
    float gradeParams[4] = {};
};

// PHASE0_MASTER_STRATEGY.md Step 2.7/AGENTS.md's "Render Pass System" - the
// render graph's own single, shared rg::RenderGraphBuilder collects EVERY
// pass declaration across BOTH the Game View AND the Scene View (whichever
// are currently visible) BEFORE the whole graph is compiled and executed
// exactly once - meaning THIS PLUGIN's own AddRenderGraphPasses() may be
// called TWICE in one frame (once per active view) before either call's own
// `execute` callback actually runs. A single, naively-overwritten global
// PluginTextureHandle would therefore be WRONG for whichever view's execute
// runs against the OTHER view's already-overwritten handle - a real, live
// bug the default Editor layout (both Game AND Scene panels visible at once)
// would hit on literally every frame. This small, fixed-size ring of state
// slots (never heap-allocated, no per-frame leak) gives each within-one-frame
// declare call its own distinct, stable-address slot - 4 is a generous margin
// over the 2 real views this engine has today.
constexpr int kMaxInFlightPassStates = 4;

struct FillPassState {
    PluginTextureHandle target;
};

FillPassState g_fillStates[kMaxInFlightPassStates];
int g_fillStateCounter = 0;

void FillSetup(IPluginPassSetupContext& ctx, void* userData)
{
    auto* state = static_cast<FillPassState*>(userData);
    ctx.WriteTexture(state->target, PluginResourceAccess::ComputeShaderWrite);
}

void FillExecute(IPluginCommandRecorder& recorder, void* userData)
{
    auto* state = static_cast<FillPassState*>(userData);

    // Byte-for-byte identical color to demo_render_feature_v2's own
    // "DemoRenderFeatureV2_Fill" (RenderFeaturePlugin.cpp, plugins/
    // demo_render_feature_v2/) - opaque RED.
    DemoV3OpsPushConstants pushConstants;
    pushConstants.colorRgba[0] = 1.0f;
    pushConstants.colorRgba[1] = 0.0f;
    pushConstants.colorRgba[2] = 0.0f;
    pushConstants.colorRgba[3] = 1.0f;

    recorder.BindTexture(0, state->target);
    // Dispatches the MAXIMUM allowed group count on both axes
    // (Locked Product Decision #2, kPluginComputeDispatchMaxGroupsPerDimension) -
    // this op's own shader (Shaders/RenderFeatureOps.comp) queries the REAL,
    // actual bound image's resolution via imageSize() and bounds-checks
    // itself per-invocation (see that shader's own header comment) - a
    // plugin has no ABI method to query a handle's own real pixel
    // dimensions today, so dispatching the generous, bounded maximum and
    // relying on the shader's own in-bounds check is the correct, safe
    // way to "cover the whole target" without knowing its exact size.
    recorder.Dispatch("gte.builtin.solid_fill", &pushConstants, sizeof(pushConstants),
        kPluginComputeDispatchMaxGroupsPerDimension, kPluginComputeDispatchMaxGroupsPerDimension, 1);
}

class DemoRenderFeatureV3 final : public IRenderFeatureModule_v3 {
public:
    GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const override
    {
        return MakeRenderFeatureDescriptor("DemoRenderFeatureV3",
            RenderFeatureStage::PostComposite, /*priority=*/0, RenderFeatureBlendMode::Replace);
    }

    void AddRenderGraphPasses(IPluginRenderPassBuilder_v3& builder) override
    {
        FillPassState& state = g_fillStates[g_fillStateCounter % kMaxInFlightPassStates];
        ++g_fillStateCounter;
        state.target = builder.GetPrivateOutputTarget();
        builder.AddComputePass("DemoRenderFeatureV3_Fill", &FillSetup, &FillExecute, &state);
    }
};

DemoRenderFeatureV3 g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v3> g_module(
    g_feature, kIRenderFeatureModule_v3_Name,
    MakeModuleInfo("DemoRenderFeatureV3Plugin", "1.0.0",
        "editor-core-separation-9 PHASE2 proof - reimplements demo_render_feature_v2's exact RED solid fill via "
        "the new generic Dispatch(\"gte.builtin.solid_fill\", ...) mechanism, for a direct pixel-parity A/B "
        "comparison (see PHASE2_COMPLETION_REPORT.md)."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
