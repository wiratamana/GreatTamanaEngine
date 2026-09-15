#pragma once

#include "../Renderer/ComputeDescriptorSet.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h" // gte::rg::ResourceState
#include "../Renderer/Texture2D.h"

#include <array>
#include <cstdint>
#include <optional>
#include <volk.h>

// task_manager/frame-debugger-3 campaign, PHASE6
// (PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md) - makes the Frame Debugger's
// Channels (All/R/G/B/A) row and Levels slider functionally real, via a
// brand-new, small, dedicated preview-compositing mechanism - explicitly
// NOT by reusing /get_texture's existing, semantically-unrelated
// channel=color|depth query parameter (PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #8).
//
// Two clean layers, mirroring this whole codebase's "pure CPU math oracle
// first, GPU shader mirrors it" discipline (AGENTS.md, "Atmosphere
// Scattering"/"GPU Vertex Skinning" both establish this exact precedent -
// copied here too, at a much smaller scale):
//   1. ApplyFrameDebuggerPreviewTransform() below - a pure, Tier-1-tested
//      CPU function describing the exact per-pixel transform (see
//      tests/Editor/FrameDebuggerPreviewProcessingTests.cpp).
//   2. src/Shaders/FrameDebuggerPreview.comp - a tiny compute shader that is
//      a DIRECT GLSL transcription of #1. If the two ever disagree, the
//      GLSL is what needs fixing, never the reverse (the CPU oracle is
//      right by definition - same rule AtmosphereMath.h/
//      VolumeTexturePreviewMath.h already established).
//
// FrameDebuggerPreviewRenderer (below) is this phase's small, dedicated,
// on-demand GPU dispatcher - it copies VolumeTexturePreviewRenderer's own
// exact SHAPE (src/Renderer/VolumeTexturePreviewRenderer.h/.cpp: one
// combined-image-sampler input, one storage-image output, a persistent
// scratch output texture reused/overwritten across calls) but NOT its
// raymarch math (unrelated). CONFIRMED during this campaign's 2nd-iteration
// strategy review: this class's own RenderPreview() issues its
// vkCmdBindPipeline/vkCmdBindDescriptorSets/vkCmdPushConstants/vkCmdDispatch
// calls directly inside a renderer.ImmediateSubmit(...) lambda - NEVER
// renderer.Dispatch() - because FrameDebuggerPanel::Build() (the only real
// caller, see Panels/FrameDebuggerPanel.cpp) runs during ImGui UI
// construction, never inside an active render-graph pass recording
// (BeginGraphPassRecording()/EndGraphPassRecording()) that Dispatch()
// requires; see VolumeTexturePreviewRenderer::RenderPreview()'s own
// identical precedent/comment for the full reasoning.
//
// GTE_ENABLE_EDITOR-only (lives under src/Editor/, exactly like
// FrameDebuggerCapture.h/FrameDebuggerHistory.h) - the ONLY real consumer,
// FrameDebuggerPanel, is itself Editor-only.
namespace gte {

class Renderer;
class RenderTexture;

// Which channel isolation the preview currently shows - matches the
// Channels row's five buttons (All/R/G/B/A) one-for-one. Declaration order
// MUST stay in sync with FrameDebuggerPreview.comp's own push-constant
// `channel` field convention (0=All, 1=R, 2=G, 3=B, 4=A) - see that
// shader's own doc comment.
enum class FrameDebuggerPreviewChannel {
    All,
    R,
    G,
    B,
    A,
};

// Pure per-pixel transform: applies the Levels remap (black/white point
// stretch, clamped to [0, 1]) to every one of `srcRgba`'s four channels
// FIRST, then isolates `channel` - replicating that ONE channel's leveled
// value across R/G/B, with alpha forced to 1.0 (matching Unity's own "view
// a single channel as a grayscale image" convention) - `All` passes the
// leveled RGBA straight through unchanged. This ordering (levels BEFORE
// channel-isolation masking) was chosen because it lets Levels meaningfully
// affect a channel view too (e.g. isolating Alpha still benefits from a
// black/white stretch on that channel), and is the ordering both this
// function and FrameDebuggerPreview.comp implement identically.
//
// `levelsWhite` is always treated as strictly greater than `levelsBlack` by
// at least 1e-5 internally (a degenerate/inverted pair never divides by
// zero or produces a negative-width remap) - callers are still expected to
// keep levelsBlack < levelsWhite themselves (see
// Panels/FrameDebuggerPanel.cpp's own DragFloatRange2 wiring) for a
// visually sensible result, but this function itself can never crash or
// produce NaN/Inf regardless.
std::array<float, 4> ApplyFrameDebuggerPreviewTransform(
    std::array<float, 4> srcRgba, FrameDebuggerPreviewChannel channel, float levelsBlack, float levelsWhite) noexcept;

// A small, self-contained, ON-DEMAND (never per-frame) GPU compute
// dispatcher - reads one source RenderTexture (PHASE3's retained per-slot
// history preview) and writes a fresh, persistent scratch Texture2D the
// panel itself displays, NEVER mutating the retained historical copy in
// place (a user must be able to flip Channels/Levels back and forth
// without losing the original captured pixels - Locked Design Decision #8).
// Owned directly by FrameDebuggerPanel (one instance per panel, exactly
// like that class already owns m_captureContext/m_history) - not a
// shared/global object.
class FrameDebuggerPreviewRenderer {
public:
    FrameDebuggerPreviewRenderer() = default;

    // Not copyable/movable - owns live Vulkan objects with no move-plumbing
    // written for them yet, exactly like VolumeTexturePreviewRenderer.
    FrameDebuggerPreviewRenderer(const FrameDebuggerPreviewRenderer&) = delete;
    FrameDebuggerPreviewRenderer& operator=(const FrameDebuggerPreviewRenderer&) = delete;

    ~FrameDebuggerPreviewRenderer();

    // Dispatches FrameDebuggerPreview.comp against `source`'s CURRENT
    // contents, applying ApplyFrameDebuggerPreviewTransform()'s exact
    // per-pixel formula for `channel`/`levelsBlack`/`levelsWhite`. `source`
    // must currently be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL - the
    // state FrameDebuggerHistory::CaptureFrame() always leaves a retained
    // preview texture in (see that method's own doc comment) - and is
    // restored to that exact same state before this call returns, mirroring
    // VolumeTexturePreviewRenderer::RenderPreview()'s own "restore
    // afterward" discipline. Lazily (re)creates this class's own persistent
    // output texture at `source`'s exact current size whenever that size
    // differs from the last call (e.g. the Game View was resized between
    // two captures). The returned reference (and OutputView()/
    // OutputSampler() below) stay valid until the NEXT RenderPreview() call
    // or this object's destruction - never store it across a frame boundary
    // without re-checking.
    const Texture2D& RenderPreview(Renderer& renderer, const RenderTexture& source, FrameDebuggerPreviewChannel channel,
        float levelsBlack, float levelsWhite);

    // The persistent scratch texture's own VkImageView/VkSampler, always
    // left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL after RenderPreview()
    // returns - VK_NULL_HANDLE until RenderPreview() has been called at
    // least once. Lets a caller (FrameDebuggerPanel::EnsurePreviewDescriptor())
    // re-read the ALREADY-processed output on a frame where nothing
    // actually changed, without re-dispatching (see this phase's own Step 2
    // "only recompute when dirty" discipline).
    VkImageView OutputView() const noexcept { return m_outputTexture.has_value() ? m_outputTexture->View() : VK_NULL_HANDLE; }
    VkSampler OutputSampler() const noexcept
    {
        return m_outputTexture.has_value() ? m_outputTexture->Sampler() : VK_NULL_HANDLE;
    }

private:
    void EnsureInitialized(Renderer& renderer);
    void EnsureOutputTexture(Renderer& renderer, int width, int height);

    // MUST match src/Shaders/FrameDebuggerPreview.comp's own
    // `layout(local_size_x = 16, local_size_y = 16) in;` exactly.
    static constexpr std::uint32_t kLocalSizeX = 16;
    static constexpr std::uint32_t kLocalSizeY = 16;

    bool m_initialized = false;
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_pipeline;
    ComputeDescriptorSet m_descriptorSet;
    std::optional<Texture2D> m_outputTexture; // Persistent, reused/overwritten across every call.
    rg::ResourceState m_outputTextureState;
    int m_outputWidth = 0;
    int m_outputHeight = 0;
};

} // namespace gte
