// plugins/demo_render_feature_v3_third/RenderFeaturePlugin.cpp
//
// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.5) - a real, PERMANENT
// _v3 demo plugin proving the THIRD shared "uber ops" operation,
// gte.builtin.color_grade, via the new generic Dispatch() mechanism. No
// existing _v2 demo plugin exercises color grade, so there is no host
// baseline to A/B pixel-diff against for this one specifically (see
// PHASE2_COMPLETION_REPORT.md's own honest explanation: color_grade shares
// the IDENTICAL RenderFeatureOps.comp pipeline, the IDENTICAL adapter
// Dispatch()/descriptor-set/opCode-stamping machinery, and the IDENTICAL
// push-constant layout already pixel-proved correct by
// demo_render_feature_v3's own solid-fill A/B and demo_render_feature_v3_second's
// own radial-vignette A/B - opCode is simply a different constant stamped
// into the SAME already-proven-correct call path).
//
// Per Shaders/RenderFeatureOps.comp's own header comment, color grade reads
// WHATEVER IS ALREADY in this plugin's own private target before this
// dispatch runs (seeded fully transparent/black) - so this demo fills its own
// private target with a solid base color FIRST, then grades it, exactly
// mirroring how a real _v2 plugin combines AddSolidFillPass + AddColorGradePass
// in one AddRenderGraphPasses() call (that same shader header comment's own
// documented convention).

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

// See demo_render_feature_v3's own identical ring-buffer doc comment for
// exactly why this exists.
constexpr int kMaxInFlightPassStates = 4;

struct GradePassState {
    PluginTextureHandle target;
};

GradePassState g_gradeStates[kMaxInFlightPassStates];
int g_gradeStateCounter = 0;

void GradeSetup(IPluginPassSetupContext& ctx, void* userData)
{
    auto* state = static_cast<GradePassState*>(userData);
    // Both the fill pass AND the grade pass write (and, for the grade pass,
    // also implicitly read via imageLoad inside the shader itself) the SAME
    // private target - declared WriteTexture for both, matching the "ops
    // compose top-to-bottom in call order" convention Shaders/RenderFeatureOps.comp's
    // own header comment documents (no separate ReadTexture declaration is
    // needed for the grade pass's own imageLoad - the shared `privateTarget`
    // storage image binding is read-write by construction, exactly like
    // _v2's own single-binding RenderFeatureOps.comp usage).
    ctx.WriteTexture(state->target, PluginResourceAccess::ComputeShaderWrite);
}

void FillExecute(IPluginCommandRecorder& recorder, void* userData)
{
    auto* state = static_cast<GradePassState*>(userData);

    // A neutral, mid-gray base - color grade has something real to work
    // with (a color grade over pure black/transparent is a fixed,
    // well-defined but visually uninteresting neutral gray-ish result, per
    // RenderFeatureOps.comp's own header comment).
    DemoV3OpsPushConstants pushConstants;
    pushConstants.colorRgba[0] = 0.6f;
    pushConstants.colorRgba[1] = 0.6f;
    pushConstants.colorRgba[2] = 0.6f;
    pushConstants.colorRgba[3] = 1.0f;

    recorder.BindTexture(0, state->target);
    recorder.Dispatch("gte.builtin.solid_fill", &pushConstants, sizeof(pushConstants),
        kPluginComputeDispatchMaxGroupsPerDimension, kPluginComputeDispatchMaxGroupsPerDimension, 1);
}

void GradeExecute(IPluginCommandRecorder& recorder, void* userData)
{
    auto* state = static_cast<GradePassState*>(userData);

    // A warm, higher-contrast, partially-desaturated grade - clearly
    // different from the flat 0.6 gray the fill pass just wrote, so this
    // pass's own visible effect is unambiguous.
    DemoV3OpsPushConstants pushConstants;
    pushConstants.colorRgba[0] = 1.0f; // tint color (warm)
    pushConstants.colorRgba[1] = 0.85f;
    pushConstants.colorRgba[2] = 0.7f;
    pushConstants.gradeParams[0] = 1.2f;  // brightness
    pushConstants.gradeParams[1] = 1.1f;  // contrast
    pushConstants.gradeParams[2] = 0.6f;  // saturation
    pushConstants.gradeParams[3] = 0.35f; // tintStrength

    recorder.BindTexture(0, state->target);
    recorder.Dispatch("gte.builtin.color_grade", &pushConstants, sizeof(pushConstants),
        kPluginComputeDispatchMaxGroupsPerDimension, kPluginComputeDispatchMaxGroupsPerDimension, 1);
}

class DemoRenderFeatureV3Third final : public IRenderFeatureModule_v3 {
public:
    GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const override
    {
        return MakeRenderFeatureDescriptor("DemoRenderFeatureV3Third",
            RenderFeatureStage::PostComposite, /*priority=*/10, RenderFeatureBlendMode::Replace);
    }

    void AddRenderGraphPasses(IPluginRenderPassBuilder_v3& builder) override
    {
        GradePassState& state = g_gradeStates[g_gradeStateCounter % kMaxInFlightPassStates];
        ++g_gradeStateCounter;
        state.target = builder.GetPrivateOutputTarget();
        builder.AddComputePass("DemoRenderFeatureV3Third_Fill", &GradeSetup, &FillExecute, &state);
        builder.AddComputePass("DemoRenderFeatureV3Third_Grade", &GradeSetup, &GradeExecute, &state);
    }
};

DemoRenderFeatureV3Third g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v3> g_module(
    g_feature, kIRenderFeatureModule_v3_Name,
    MakeModuleInfo("DemoRenderFeatureV3ThirdPlugin", "1.0.0",
        "editor-core-separation-9 PHASE2 proof - a solid-fill-then-color-grade pair via the new generic "
        "Dispatch(\"gte.builtin.solid_fill\"/\"gte.builtin.color_grade\", ...) mechanism, proving the THIRD "
        "shared uber-op works generically (no existing _v2 demo baseline to A/B against - see "
        "PHASE2_COMPLETION_REPORT.md)."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
