#pragma once

#include "ComputeDescriptorSet.h"
#include "ComputePipeline.h"
#include "RenderGraph/RenderGraphBarrierPlanner.h" // gte::rg::ResourceState
#include "Texture2D.h"
#include "VolumeTarget.h"

#include <cstdint>
#include <optional>
#include <vector>
#include <volk.h>

namespace gte {

class Renderer;

// atmosphere-scattering-2 campaign, Phase 4
// (task_manager/atmosphere-scattering-2/PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md)
// - which raw-texel -> density/color interpretation VolumeTexturePreview.comp
// uses, AND (as of atmosphere-scattering-3 campaign, Phase 2 -
// task_manager/atmosphere-scattering-3/PHASE2_ATMOSPHERE_AWARE_PREVIEW_CAMERA_FRAMING.md)
// which camera/proxy-box FRAMING VolumeTexturePreviewRenderer::RenderPreview()
// uses. Auto-selected by RenderPreview()'s own caller (Application.cpp) based
// on the requested texture_name string - never exposed as a new HTTP query
// parameter (see PHASE0_MASTER_STRATEGY.md's own Locked Design Decision 6).
enum class VolumeTexturePreviewInterpretation : std::int32_t {
    GenericDensityInAlpha = 0, // network-impl-6's original, still-default interpretation - UNCHANGED.
    AtmosphereAerialPerspective = 1, // atmosphere-scattering-2 Phase 4 - see VolumeTexturePreview.comp's own doc comment.
};

// network-impl-6 campaign, Phase 3. A small, self-contained, ON-DEMAND
// (never per-frame) GPU compute renderer that raymarches an arbitrary live
// VolumeTexture into a fixed-size 2D RGBA8 thumbnail - the "Volume mode"
// preview Unity's own Texture3D inspector uses, built for an LLM/AI agent
// fetching it over HTTP (GET /get_texture, see network-impl-6's
// PHASE0_MASTER_STRATEGY.md) rather than for a human-facing Editor panel.
//
// Driven ENTIRELY by ONE Renderer::ImmediateSubmit() call per RenderPreview()
// (plus a second, internal one inside Renderer::CaptureImagePixels() for the
// readback) - mirrors GpuSkinningValidation.cpp's own "self-contained,
// hand-rolled dispatch + barrier, no gte::rg::RenderGraph dependency" shape,
// NOT ComputeBlurValidation's (that class is a real, per-frame RenderGraph
// compute PASS - see this phase's own Step 2 for the full correction). This
// class NEVER calls Renderer::Dispatch() (gated to render-graph-pass
// recording only - see Step 2) - it issues its own raw vkCmdBindPipeline/
// vkCmdBindDescriptorSets/vkCmdPushConstants/vkCmdDispatch calls instead.
// Lives in core src/Renderer/ (NOT src/Editor/) since it must compile and
// work with GTE_ENABLE_EDITOR=OFF (this feature follows GET /get_texture's
// own gating exactly - see PHASE0's Locked Design Decision 4).
class VolumeTexturePreviewRenderer {
public:
    VolumeTexturePreviewRenderer() = default;

    // Not copyable/movable - owns live Vulkan objects with no move-
    // plumbing written for them yet (this class is a singleton-per-
    // Renderer-lifetime member, exactly like AtmosphereLutRenderer -
    // never needs to be copied or moved).
    VolumeTexturePreviewRenderer(const VolumeTexturePreviewRenderer&) = delete;
    VolumeTexturePreviewRenderer& operator=(const VolumeTexturePreviewRenderer&) = delete;

    ~VolumeTexturePreviewRenderer();

    struct CapturedRawPixels {
        std::vector<std::uint8_t> pixels; // Tightly packed width*height*4 RGBA8 bytes.
        int width = 0;
        int height = 0;
    };

    // Synchronously renders ONE fixed-camera "Volume mode" raymarch
    // thumbnail of `volume` (its CURRENT contents, at `previousState`'s
    // real, caller-supplied current ResourceState) and reads it back to
    // the CPU. Blocking (uses Renderer::ImmediateSubmit() internally, more
    // than once) - acceptable ONLY because this is invoked at most once per
    // network request, exactly like Renderer::CaptureImagePixels() itself
    // (see that method's own doc comment). `volume`'s own image layout is
    // restored to `previousState` before this method returns, exactly
    // mirroring CaptureImagePixels()'s existing "restore afterward"
    // discipline (a later graph-recorded frame touching the SAME volume
    // texture must see it in the state it expects).
    // `interpretation` selects the raw-texel -> density/color derivation
    // VolumeTexturePreview.comp uses, AND (as of atmosphere-scattering-3
    // campaign, Phase 2) which camera/proxy-box FRAMING this method itself
    // uses internally (see the enum's own doc comment above) -
    // this codebase's own convention favors explicit call sites over relying
    // on the default, so pass it explicitly at every real call site even
    // though a default is provided here for convenience/safety.
    CapturedRawPixels RenderPreview(Renderer& renderer, const VolumeTarget& volume, const rg::ResourceState& previousState,
        VolumeTexturePreviewInterpretation interpretation = VolumeTexturePreviewInterpretation::GenericDensityInAlpha);

private:
    void EnsureInitialized(Renderer& renderer);

    static constexpr int kOutputWidth = 256;
    static constexpr int kOutputHeight = 256;
    static constexpr int kStepCount = 64;
    static constexpr float kDensityScale = 4.0f; // Tunable - see how-to reference material's own "2-12, tune per asset" note; a single fixed default is enough for this campaign's generic debug-visualization goal (Locked Design Decision 9).

    // MUST match src/Shaders/VolumeTexturePreview.comp's own
    // `layout(local_size_x = 16, local_size_y = 16) in;` exactly.
    static constexpr std::uint32_t kLocalSizeX = 16;
    static constexpr std::uint32_t kLocalSizeY = 16;

    bool m_initialized = false;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_volumeSampler = VK_NULL_HANDLE; // Trilinear, clamp-to-edge x3 - created once, owned by this class.
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_pipeline;
    // A ComputeDescriptorSet (NOT a raw VkDescriptorSet) so this class can
    // call its own .Rewrite() directly - matches ComputeBlurValidation's
    // identical member shape. Allocated once; Rewrite()'s binding-update
    // happens EVERY call (unlike GpuSkinningRigCache's "once per model" -
    // this class serves a DIFFERENT volume on every call, so binding 0 must
    // be refreshed every time - see .cpp). Binding 1 (the output image)
    // never actually changes across calls, but is simplest to rewrite
    // alongside binding 0 in the same Rewrite() call every time.
    ComputeDescriptorSet m_descriptorSet;
    std::optional<Texture2D> m_outputTexture; // Persistent, created once, reused/overwritten across every call (Locked Design Decision 8).
    // Tracks m_outputTexture's REAL current ResourceState across calls -
    // initialized to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL in
    // EnsureInitialized() (the real, confirmed post-construction layout
    // Texture2D.h's own doc comment documents), then GENERAL after every
    // RenderPreview() call (see .cpp).
    rg::ResourceState m_outputTextureState;
};

} // namespace gte
