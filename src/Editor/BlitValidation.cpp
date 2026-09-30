#include "BlitValidation.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderPassToggleGuard.h"

#include <array>

namespace gte {

namespace {
// A distinctive, non-default depth value - never 0.0f/1.0f (both easily
// confused with "never written"/the standard far-clear value) - so
// "BlitValidationDepthBlit" produces a genuinely verifiable, non-degenerate
// grayscale value via GET /get_texture?texture_name=BlitValidationOutput&channel=depth.
constexpr float kBlitValidationSourceClearDepth = 0.3f;
} // namespace

void BlitValidation::EnsureInitialized(Renderer& renderer)
{
    if (m_source.has_value()) {
        return;
    }
    // R8G8B8A8_UNORM, never the swapchain's own negotiated format - mirrors
    // ComputeBlurValidation::EnsureInitialized()'s own identical reasoning
    // (this texture is never bound to the same Pipeline as the swapchain/
    // Game/Scene views).
    m_source.emplace(renderer.CreateRenderTexture(512, 512, VK_FORMAT_R8G8B8A8_UNORM,
        "BlitValidationSource", "BlitValidationSourceDepth", /*allowStorageImageAccess=*/false));
    m_output.emplace(renderer.CreateRenderTexture(1024, 1024, VK_FORMAT_R8G8B8A8_UNORM,
        "BlitValidationOutput", "BlitValidationOutputDepth", /*allowStorageImageAccess=*/false));
}

rg::TextureHandle BlitValidation::AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    EnsureInitialized(renderer);

    const rg::TextureHandle sourceHandle =
        builder.ImportTexture("BlitValidationSource", m_source->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
    const rg::TextureHandle outputHandle =
        builder.ImportTexture("BlitValidationOutput", m_output->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

    // docs/conventions/render-pass-toggle-honesty.md's iron rule, using this
    // codebase's own MANDATED helper for it (RenderPassToggleGuard.h's
    // ShouldDeclareBuiltInPassThisFrame()) - both passes below are declared
    // via a DIRECT builder call, bypassing the generic
    // RenderPipeline::DeclareOnePhase() flush loop that would otherwise gate
    // them for free. Independent calls (never one shared consult) mirror
    // GBufferValidation::AddPass()'s own established "independent per-half
    // gating" precedent - each of this pass's rows in the "Render Graph"
    // panel must independently mean something.
    // ShouldDeclareBuiltInPassThisFrame() itself already returns true
    // unconditionally when `toggleRegistry` is nullptr, so no separate null
    // check is needed here.
    if (rg::ShouldDeclareBuiltInPassThisFrame(toggleRegistry, "BlitValidationSourceFill")) {
        // A tiny, distinctive, clear-only Graphics pass - real content for
        // the blits below to genuinely copy/scale, never a degenerate
        // all-black texture that would make a "did the blit actually run"
        // screenshot ambiguous. Also clears the source's own companion depth
        // buffer to a distinctive, known value (kBlitValidationSourceClearDepth)
        // - real content for "BlitValidationDepthBlit" below to genuinely
        // copy/scale, mirroring GBufferValidation::AddPass()'s own precedent
        // of pairing a color write with a real WriteDepthStencilAttachment()
        // call on the very same pass.
        builder.AddRenderPass(
            "BlitValidationSourceFill", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::Debug,
            [sourceHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(sourceHandle, std::array<float, 4>{ 0.85f, 0.15f, 0.55f, 1.0f });
                pass.WriteDepthStencilAttachment(sourceHandle, kBlitValidationSourceClearDepth);
            },
            [](rg::PassContext&) { /* clear-only - no draws issued */ },
            rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterEverything);
    }

    if (rg::ShouldDeclareBuiltInPassThisFrame(toggleRegistry, "BlitValidationBlit")) {
        rg::BlitSpec spec;
        spec.src = sourceHandle;
        spec.dst = outputHandle;
        builder.AddBlitPass("BlitValidationBlit", spec, rg::RenderPassEvent::AfterEverything,
            rg::ViewScope::Shared, rg::RenderPassCategory::Debug);
    }

    // editor-core-separation-26 campaign, PHASE6 (Locked Decision 4 / Step 1
    // goal #5, resolved via `ask_questions` - see PHASE6_COMPLETION_REPORT.md)
    // - a REAL depth-to-depth blit, exercised for real ONLY when
    // Renderer::SupportsDepthBlit() reports true on THIS machine's ACTUAL,
    // real, running GPU/driver - a genuine, engine-checked runtime branch,
    // never a documented-only trust, and never unconditionally declared
    // regardless of hardware support (that would be a real, silent
    // corruption/validation-error hazard on a device that lacks
    // VK_FORMAT_FEATURE_BLIT_SRC_BIT/_DST_BIT for its own real depth
    // format - the debug-assert inside RenderGraph::ExecuteCompiledGraph()'s
    // own execution branch is a safety net for a DEVELOPMENT build only, it
    // compiles away entirely in a release/NDEBUG build).
    if (renderer.SupportsDepthBlit()
        && rg::ShouldDeclareBuiltInPassThisFrame(toggleRegistry, "BlitValidationDepthBlit")) {
        rg::BlitSpec depthSpec;
        depthSpec.src = sourceHandle;
        depthSpec.dst = outputHandle;
        depthSpec.srcIsDepth = true;
        depthSpec.dstIsDepth = true;
        builder.AddBlitPass("BlitValidationDepthBlit", depthSpec, rg::RenderPassEvent::AfterEverything,
            rg::ViewScope::Shared, rg::RenderPassCategory::Debug);
    }

    return outputHandle;
}

} // namespace gte
