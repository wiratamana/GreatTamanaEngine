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
    // editor-core-separation-9 campaign, PHASE4
    // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.2) - the
    // FETCHING half of this campaign's real, minimal 2-plugin blackboard
    // proof. Populated at declare-time (AddRenderGraphPasses(), below) via
    // IPluginBlackboard::Fetch() - `blurStrengthFetched` records whether the
    // fetch actually succeeded THIS call, so VignetteExecute() below can
    // fall back to this plugin's own original, PHASE2-era fixed radius
    // (0.65) if plugins/demo_render_feature_v3/ (the publisher) were ever
    // NOT loaded - never a crash, never an assumed value.
    bool blurStrengthFetched = false;
    float blurStrength = 0.0f;
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

    // Base parameters are byte-for-byte identical to
    // demo_render_feature_v2_second's own "DemoRenderFeatureV2Second_Vignette"
    // (RenderFeaturePlugin.cpp, plugins/demo_render_feature_v2_second/):
    // centerX=0.5, centerY=0.5, innerRadius=0.15, outerRadius=0.65,
    // color=BLUE opaque. editor-core-separation-9 campaign, PHASE4
    // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.2) - the
    // outer radius is now widened by the blackboard-fetched
    // "DemoV3.BlurStrength" value (published by plugins/demo_render_feature_v3/,
    // fetched below in AddRenderGraphPasses()) whenever the fetch succeeded -
    // a REAL, VISIBLE, live proof that a value handed off through
    // IPluginBlackboard actually reached and influenced this independently-
    // loaded plugin's own rendering, not merely "was logged and not crashed".
    // Falls back to the original, unwidened 0.65 radius if the fetch ever
    // fails (e.g. the publishing plugin is not loaded this session).
    DemoV3OpsPushConstants pushConstants;
    pushConstants.colorRgba[0] = 0.0f;
    pushConstants.colorRgba[1] = 0.0f;
    pushConstants.colorRgba[2] = 1.0f;
    pushConstants.colorRgba[3] = 1.0f;
    pushConstants.centerAndRadius[0] = 0.5f;
    pushConstants.centerAndRadius[1] = 0.5f;
    pushConstants.centerAndRadius[2] = 0.15f;
    pushConstants.centerAndRadius[3] = state->blurStrengthFetched ? 0.65f * (1.0f + state->blurStrength) : 0.65f;

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

        // editor-core-separation-9 campaign, PHASE4
        // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.2) - the
        // FETCHING half of this campaign's real, minimal 2-plugin blackboard
        // proof: this plugin (PreUI, priority 0) is ALWAYS declared strictly
        // AFTER every PostComposite entry (RenderFeatureCompositor::
        // ContributeRenderGraphPasses()'s own combined list order -
        // m_postComposite first, m_preUi second) - so by the time THIS
        // Fetch() call runs, plugins/demo_render_feature_v3/'s own earlier
        // Publish() call (same frame, same view) has already happened. This
        // plugin's own source has zero compile-time or link-time dependency
        // on demo_render_feature_v3 - the two are mutually unaware,
        // independently loaded .dlls that only agree on a shared, documented
        // string key ("DemoV3.BlurStrength") and PluginBlackboardValueKind
        // (Float).
        PluginBlackboardValue blurStrength;
        state.blurStrengthFetched =
            builder.Blackboard().Fetch("DemoV3.BlurStrength", PluginBlackboardValueKind::Float, blurStrength);
        state.blurStrength = state.blurStrengthFetched ? blurStrength.f : 0.0f;

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
