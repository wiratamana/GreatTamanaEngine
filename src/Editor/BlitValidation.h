#pragma once

// editor-core-separation-26 campaign, PHASE6 (Locked Decision 3) - a small,
// permanent, Debug-category live proof that RenderGraphBuilder::AddBlitPass()
// (PHASE5) genuinely works: fills its own persistent 512x512
// "BlitValidationSource" texture with a distinctive clear color/depth every
// frame, then blits it into its own persistent 1024x1024
// "BlitValidationOutput" texture. Far simpler than ComputeBlurValidation/
// GBufferValidation - no compute pipeline, no descriptor set, no resize logic
// (both textures are FIXED size, forever) - verified purely via
// GET /get_texture screenshots, no ImGui panel/checkbox/descriptor-set
// apparatus at all (Locked Decision 3; unlike ComputeBlurValidation/
// GBufferValidation, which ALSO wanted a live, in-Editor toggleable display -
// a requirement this pass's own acceptance bar does not ask for).
//
// A THIRD pass, "BlitValidationDepthBlit" (a real depth-to-depth blit,
// srcIsDepth/dstIsDepth both true), is declared ONLY when
// Renderer::SupportsDepthBlit() reports true on the actual running
// GPU/driver - a genuine, engine-checked runtime branch (Locked Decision 4),
// never unconditionally declared regardless of hardware support.
//
// Owned by ImGuiEditorLayer, alongside m_blurValidation/m_gbufferValidation -
// exposed to Core purely through one new IEditorLayer method
// (AddBlitValidationPass(), see EditorLayer.h), mirroring
// AddBlurValidationPass()'s own exact boundary.

#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderTexture.h"

#include <optional>

namespace gte {

namespace rg {
class RenderPassToggleRegistry;
} // namespace rg

class Renderer;

class BlitValidation {
public:
    BlitValidation() = default;
    ~BlitValidation() = default;

    BlitValidation(const BlitValidation&) = delete;
    BlitValidation& operator=(const BlitValidation&) = delete;
    BlitValidation(BlitValidation&&) = delete;
    BlitValidation& operator=(BlitValidation&&) = delete;

    // Declares this frame's two passes into `builder` (a small clear-only
    // "BlitValidationSourceFill" graphics pass, then the real
    // "BlitValidationBlit" pass, via RenderGraphBuilder::AddBlitPass()) -
    // lazily builds this object's own persistent RenderTextures the first
    // time this is called (needs a live Renderer, so can't happen in the
    // default constructor). Always declares (this pass always runs when an
    // Editor layer is present) - `toggleRegistry` (default nullptr,
    // mirroring ComputeBlurValidation/GBufferValidation's own established
    // precedent) still lets docs/conventions/render-pass-toggle-honesty.md's
    // iron rule be honestly satisfied for both of this pass's own two rows
    // in the "Render Graph" panel, since both are declared via a DIRECT
    // builder call, bypassing the generic RenderPipeline::DeclareOnePhase()
    // flush loop that would otherwise gate them for free.
    //
    // Returns the persistent "BlitValidationOutput" texture's handle - the
    // CALLER must add it to this call's own finalOutputs root set, or
    // RenderGraphCompiler culling would silently drop the whole
    // "BlitValidationBlit" pass every frame (it has zero in-frame readers).
    rg::TextureHandle AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

private:
    void EnsureInitialized(Renderer& renderer);

    std::optional<RenderTexture> m_source; // Fixed 512x512, forever.
    std::optional<RenderTexture> m_output; // Fixed 1024x1024, forever.
};

} // namespace gte
