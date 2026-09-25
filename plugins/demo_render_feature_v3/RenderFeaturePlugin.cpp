// plugins/demo_render_feature_v3/RenderFeaturePlugin.cpp
//
// editor-core-separation-9 campaign, PHASE3
// (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md) - this is the
// phase that answers the whole campaign's original motivating question for
// real, end-to-end, with a live, mathematically-verified pixel proof: "a
// compute shader writes a texture, a later pass reads it". A real,
// permanent, committed 2-pass GPU downsample-blur, using ONLY generic,
// already-shipped `_v3` primitives (CreateTexture/ReadTexture/WriteTexture/
// Dispatch/DrawFullscreenTriangle) plus exactly ONE brand-new, tiny,
// additive registry operation, `gte.builtin.blit_fullscreen` (a minimal
// fullscreen-triangle passthrough fragment shader) - proving, a SECOND
// time, that a new operation lands with zero IPluginRenderPassBuilder_v3
// interface change, this time for a GRAPHICS-kind operation (PHASE2's own
// `gte.builtin.box_blur` was the first proof, for a COMPUTE-kind one).
//
// PASS 1 ("DemoRenderFeatureV3_Downsample", compute): READS "SceneColor"
// (this view's already-composited scene, Locked Product Decision #3),
// WRITES a brand-new, transient, half-res texture via
// Dispatch("gte.builtin.box_blur", ...) - a genuine downsample-blur, not
// merely a same-size blur, since Shaders/BoxBlur.comp samples its source by
// NORMALIZED UV (see that shader's own header comment).
//
// PASS 2 ("DemoRenderFeatureV3_UpsamplePresent", graphics): READS that SAME
// half-res texture PASS 1 just wrote, WRITES this plugin's own
// GetPrivateOutputTarget() via DrawFullscreenTriangle("gte.builtin.blit_fullscreen",
// ...) - the render graph's own real dependency-edge scan inserts the
// correct compute-write -> fragment-read barrier automatically, exactly
// like this engine's own internal Atmosphere LUT passes already do (Design
// Doc Part 1.3) - PROVING the whole campaign's original question for real.
//
// REPLACES this plugin's own former PHASE2-era content (a solid RED fill
// via Dispatch("gte.builtin.solid_fill", ...), used ONLY for PHASE2's own
// pixel-parity A/B proof against demo_render_feature_v2 - that proof is
// already complete and permanently recorded in
// task_manager/editor-core-separation-9/PHASE2_COMPLETION_REPORT.md, so
// changing this plugin's live behavior here does not erase or invalidate
// it). This is, and was always documented to be
// (plugins/demo_render_feature_v3/CMakeLists.txt's own PHASE2-era header
// comment), "the PRIMARY _v3 demo plugin PHASE3 will ADD its own 2-pass GPU
// blur demo passes to (never a second, separate folder)" - this phase's own
// worked-example pseudocode (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md
// Step 3.3) replaces this plugin's ENTIRE AddRenderGraphPasses() body, so
// this file follows that literally rather than layering the new blur passes
// alongside the old Fill pass (which would leave the Fill pass's own
// private-output write immediately overwritten, later the SAME frame, by
// the blur pass's own write to the exact same handle - a wasted, dead GPU
// pass, not a meaningful demonstration of anything).

#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/RenderFeatureDescriptor.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder_v3.h"
#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

// PHASE0_MASTER_STRATEGY.md Step 2.7/AGENTS.md's "Render Pass System" - the
// render graph's own single, shared rg::RenderGraphBuilder collects EVERY
// pass declaration across BOTH the Game View AND the Scene View (whichever
// are currently visible) BEFORE the whole graph is compiled and executed
// exactly once - meaning THIS PLUGIN's own AddRenderGraphPasses() may be
// called TWICE in one frame (once per active view) before either call's own
// `execute` callback actually runs. A single, naively-overwritten global
// state struct would therefore be WRONG for whichever view's execute runs
// against the OTHER view's already-overwritten handles - a real, live bug
// the default Editor layout (both Game AND Scene panels visible at once)
// would hit on literally every frame. This small, fixed-size ring of state
// slots (never heap-allocated, no per-frame leak) gives each within-one-frame
// declare call its own distinct, stable-address slot - 4 is a generous
// margin over the 2 real views this engine has today (mirrors this same
// plugin's own PHASE2-era FillPassState ring precedent exactly).
constexpr int kMaxInFlightPassStates = 4;

// PHASE3's own chosen fixed half-res demo blur target size. A `_v3` plugin
// has NO ABI method to query "SceneColor"'s own real, current pixel
// dimensions (no such accessor exists anywhere in
// IPluginRenderPassBuilder_v3.h/IPluginPassSetupContext/IPluginCommandRecorder
// - the Design Doc's own curated vocabulary never exposed one - mirrors
// this same plugin's own PHASE2-era Fill pass, which hit the identical gap
// and documented it identically: "a plugin has no ABI method to query a
// handle's own real pixel dimensions today"). This is SAFE and CORRECT for
// Shaders/BoxBlur.comp's own math: it samples `sourceTexture` via
// NORMALIZED UV (never a literal texel coordinate), so a destination
// resolution that does not literally equal "SceneColor's real size / 2"
// still produces a mathematically well-defined, correct downsample-blur -
// this demo is "blur the whole SceneColor down into a fixed 640x360
// target", not "blur it down into exactly half of whatever its own current
// size happens to be". Documented here honestly, and in
// PHASE3_COMPLETION_REPORT.md, rather than silently assumed.
constexpr std::uint32_t kHalfResWidth = 640;
constexpr std::uint32_t kHalfResHeight = 360;

// MUST match Shaders/BoxBlur.comp's own `layout(local_size_x = 16,
// local_size_y = 16) in;` exactly (see that shader's own header comment on
// why this pairing is a hand-maintained, per-shader convention - this
// engine deliberately has no shader reflection).
constexpr std::uint32_t kBoxBlurLocalSize = 16;

std::uint32_t CeilDiv(std::uint32_t value, std::uint32_t divisor)
{
    return (value + divisor - 1) / divisor;
}

struct BlurPassState {
    PluginTextureHandle sceneColor;
    PluginTextureHandle halfRes;
    PluginTextureHandle privateOutput;
};

BlurPassState g_blurStates[kMaxInFlightPassStates];
int g_blurStateCounter = 0;

// --- Pass 1: "DemoRenderFeatureV3_Downsample" (compute) ---------------------

void DownsampleSetup(IPluginPassSetupContext& ctx, void* userData)
{
    auto* state = static_cast<BlurPassState*>(userData);
    ctx.ReadTexture(state->sceneColor, PluginResourceAccess::ComputeShaderRead);
    ctx.WriteTexture(state->halfRes, PluginResourceAccess::ComputeShaderWrite);
}

void DownsampleExecute(IPluginCommandRecorder& recorder, void* userData)
{
    auto* state = static_cast<BlurPassState*>(userData);

    // Binding convention matches gte.builtin.box_blur's own registered slot
    // table exactly (PluginRenderOperationRegistry::RegisterBoxBlur()):
    // slot 0 = read-only sourceTexture (this view's "SceneColor"), slot 1 =
    // write-only destinationImage (this pass's own new half-res texture).
    recorder.BindTexture(0, state->sceneColor);
    recorder.BindTexture(1, state->halfRes);

    // (width, height) push-constant pair - the EXACT byte layout
    // gte.builtin.box_blur's own maxParamBytes (8 bytes = 2 x uint32_t)
    // requires (PluginRenderOperationRegistry::RegisterBoxBlur()) - the
    // destination's OWN real dimensions (kHalfResWidth/kHalfResHeight), not
    // the source's - see Shaders/BoxBlur.comp's own PushConstants block.
    const std::uint32_t dims[2] = { kHalfResWidth, kHalfResHeight };

    // Real, exact group counts (ceiling-divided by BoxBlur.comp's own
    // 16x16 local size) - UNLIKE this same plugin's own PHASE2-era Fill
    // pass (which had to dispatch the generous, bounded MAXIMUM group count
    // because it never knew its own target's real size), this pass DOES
    // know halfRes's real size exactly - WE chose it, via CreateTexture()'s
    // own literal desc, in AddRenderGraphPasses() below - so dispatching
    // the precise, minimal group count here is the correct, efficient
    // choice, not merely an allowed one.
    recorder.Dispatch("gte.builtin.box_blur", dims, sizeof(dims), CeilDiv(kHalfResWidth, kBoxBlurLocalSize),
        CeilDiv(kHalfResHeight, kBoxBlurLocalSize), 1);
}

// --- Pass 2: "DemoRenderFeatureV3_UpsamplePresent" (graphics) ---------------

void UpsamplePresentSetup(IPluginPassSetupContext& ctx, void* userData)
{
    auto* state = static_cast<BlurPassState*>(userData);
    ctx.ReadTexture(state->halfRes, PluginResourceAccess::ShaderRead);
    // Clears to fully transparent black first - this op's own single
    // full-screen triangle then immediately overwrites every pixel anyway
    // (a literal passthrough, Shaders/PluginBlitFullscreen.frag), so the
    // clear value itself is never actually visible in the final image.
    ctx.WriteColorAttachment(state->privateOutput, /*hasClearColor=*/true, 0.0f, 0.0f, 0.0f, 0.0f);
}

void UpsamplePresentExecute(IPluginCommandRecorder& recorder, void* userData)
{
    auto* state = static_cast<BlurPassState*>(userData);

    // gte.builtin.blit_fullscreen's own registered slot table
    // (PluginRenderOperationRegistry::RegisterBlitFullscreen()) has exactly
    // ONE slot: slot 0 = sourceTexture, the SAME halfRes handle Pass 1 just
    // wrote - the render graph's own real dependency-edge scan inserts the
    // correct compute-write -> fragment-read barrier automatically.
    recorder.BindTexture(0, state->halfRes);
    // No parameters at all - a literal passthrough.
    recorder.DrawFullscreenTriangle("gte.builtin.blit_fullscreen", nullptr, 0);
}

class DemoRenderFeatureV3 final : public IRenderFeatureModule_v3 {
public:
    GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const override
    {
        // AlphaOver (per this phase's own plan, Step 3.3) - so this plugin's
        // blurred result visibly composites over whatever ran before it in
        // the SAME stage, rather than an unconditional Replace overwrite.
        // Same stage/priority this plugin already used for its own
        // PHASE2-era Fill pass (PostComposite, priority 0) - unchanged, no
        // real production feature collides with it (the only other loaded
        // demo plugins at this priority/stage are this campaign's own
        // sibling demo plugins, an already-documented, cosmetic-only
        // tie-break case - see PHASE2_COMPLETION_REPORT.md).
        return MakeRenderFeatureDescriptor(
            "DemoRenderFeatureV3", RenderFeatureStage::PostComposite, /*priority=*/0, RenderFeatureBlendMode::AlphaOver);
    }

    void AddRenderGraphPasses(IPluginRenderPassBuilder_v3& builder) override
    {
        BlurPassState& state = g_blurStates[g_blurStateCounter % kMaxInFlightPassStates];
        ++g_blurStateCounter;

        // R14 - "SceneColor" always succeeds today (Locked Product Decision #3).
        builder.TryGetNamedTexture("SceneColor", state.sceneColor);
        state.privateOutput = builder.GetPrivateOutputTarget();

        PluginTextureDesc halfResDesc;
        halfResDesc.width = kHalfResWidth;
        halfResDesc.height = kHalfResHeight;
        halfResDesc.format = PluginTextureDesc::Format::Rgba8Unorm;
        state.halfRes = builder.CreateTexture("DemoV3.HalfResBlur", halfResDesc);

        // editor-core-separation-9 campaign, PHASE4
        // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.2) - the
        // PUBLISHING half of this campaign's real, minimal 2-plugin
        // blackboard proof: this plugin (DemoRenderFeatureV3, PostComposite
        // priority 0) Publish()es a small Float value under a namespaced key
        // BEFORE plugins/demo_render_feature_v3_second/ (PreUI, priority 0)
        // gets its own AddRenderGraphPasses() call this same frame -
        // PostComposite entries are always declared before ANY PreUI entry
        // in RenderFeatureCompositor::ContributeRenderGraphPasses()'s own
        // combined list (m_postComposite is inserted first, m_preUi second -
        // see that method's own source), so this ordering is real and
        // load-bearing, not a coincidence of priority values. Neither this
        // plugin's own code, nor demo_render_feature_v3_second's own code,
        // needed to change because of the OTHER one existing - each only
        // knows the shared, documented key/kind convention below, exactly
        // the Design Doc's own "neither plugin's own code needed to change"
        // framing this proof exists to demonstrate.
        PluginBlackboardValue blurStrength;
        blurStrength.kind = PluginBlackboardValueKind::Float;
        blurStrength.f = 0.5f;
        builder.Blackboard().Publish("DemoV3.BlurStrength", blurStrength);

        // Pass 1: compute downsample-blur - READS state.sceneColor, WRITES
        // state.halfRes (a DIFFERENT resolution - a genuine downsample).
        builder.AddComputePass("DemoRenderFeatureV3_Downsample", &DownsampleSetup, &DownsampleExecute, &state);

        // Pass 2: graphics upsample-present - READS state.halfRes (the SAME
        // handle Pass 1 just wrote), WRITES this plugin's own private
        // output target. This exact shape is the concrete, permanent,
        // committed, byte-for-byte realization of the Design Doc's own
        // Part 4.7 worked example.
        builder.AddGraphicsPass(
            "DemoRenderFeatureV3_UpsamplePresent", &UpsamplePresentSetup, &UpsamplePresentExecute, &state);
    }
};

DemoRenderFeatureV3 g_feature;
SingleCapabilityPluginModule<IRenderFeatureModule_v3> g_module(
    g_feature, kIRenderFeatureModule_v3_Name,
    MakeModuleInfo("DemoRenderFeatureV3Plugin", "1.0.0",
        "editor-core-separation-9 PHASE3 - the real, permanent 2-pass GPU downsample-blur demo answering this whole "
        "campaign's original motivating question end-to-end: a compute pass (\"DemoRenderFeatureV3_Downsample\") "
        "writes a half-res texture via gte.builtin.box_blur, a later graphics pass "
        "(\"DemoRenderFeatureV3_UpsamplePresent\") reads it via the brand-new gte.builtin.blit_fullscreen operation "
        "(see PHASE3_COMPLETION_REPORT.md)."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
