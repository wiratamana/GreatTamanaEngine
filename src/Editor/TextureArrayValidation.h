#pragma once

// task_manager/better-render-pass-7 campaign (better-render-pass-3 campaign,
// BLOCK5 - Array/Cubemap Texture Resources), PHASE5
// (PHASE5_MANUAL_TIER2_VERIFICATION_AND_LIVE_SMOKE_TEST.md) - a small,
// additive, debug-only "TextureArray Validation" pass pair, mirroring
// src/Editor/GBufferValidation.h/.cpp's exact shape (lazy init against a
// live Renderer&, persistent owned output RenderTextures, declared via
// RenderGraphBuilder::AddRenderPass(), ViewScope::SceneView +
// RenderPassCategory::Debug, toggled by its own small Scene-panel checkbox,
// zero effect on the default Game View) - this campaign's own first real,
// live-GPU consumer of BLOCK5's new TextureArray resource kind.
//
// Produces the manual/Tier-2 evidence BLOCK5_ARRAY_AND_CUBEMAP_TEXTURE_RESOURCES.txt's
// Section 6 (Verification) item 4 requires: "a throwaway COMPUTE pass writes
// a distinct solid color into each of 4 layers of a real TextureArray ...
// then a readback/screenshot proves all 4 layers are genuinely distinct, not
// aliased onto the same memory." TWO real compute passes, mirroring
// AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()'s own
// CONFIRMED, already-shipped precedent for "extract one layer/slice of a
// multi-layer GPU image into a plain, separately-capturable 2D texture" -
// TextureArray itself can NEVER be fetched via GET /get_texture directly, no
// RenderGraphDebugTextureRegistry counterpart exists for it (a locked MVP
// decision, see task_manager/better-render-pass-7/PHASE0_MASTER_STRATEGY.md's
// non-goal #10):
//
// - Pass A ("TextureArrayValidationFill", Compute) - builder.CreateTextureArray()
//   (POOLED/transient - this call's own real point is to prove
//   RenderGraphResourcePool::AcquireTextureArray() genuinely reuses this
//   exact entry frame-to-frame, observable via the Editor's "Memory" panel
//   resource count staying stable) a 64x64x4 TextureArrayDesc{ hasDepth =
//   false, usage = TextureUsage::Storage } ("ManualVerifyArray" - the EXACT
//   descriptor PHASE5's own source document requires, since the struct's
//   own default, hasDepth = true, is a depth image that cannot be
//   imageStore'd into by a compute shader at all), then writes 4 DISTINCT
//   solid colors (red/green/blue/yellow) into its 4 layers via ONE compute
//   dispatch (image2DArray, imageStore indexed by gl_GlobalInvocationID.z).
// - Pass B x4 ("TextureArrayValidationSliceN", Compute) - reads
//   "ManualVerifyArray" back (sampler2DArray, the whole-array view/sampler
//   TextureArray2D::Target() already exposes - see Section 5's own
//   documented "no per-layer view" MVP limitation; this pass only ever
//   reads, never writes, a specific layer, so the whole-array view is
//   sufficient) and writes ONE layer each into its own persistent,
//   separately-registered 2D RenderTexture output
//   ("ManualVerifyArrayLayer0".."3") - each becomes independently
//   GET /get_texture-capturable with zero further networking changes, since
//   each is a plain Texture-kind resource already covered by the existing
//   RenderGraphDebugTextureRegistry.
//
// GTE_LOG_INFO (never printf/std::cout - see AGENTS.md "Logging") reports
// the resolved VkImage handle behind "ManualVerifyArray" every time Pass A
// runs, so a live verification session can correlate a log line (via
// GET /get_logs) with what GET /get_texture captures.
//
// Clean-up status: per PHASE5_COMPLETION_REPORT.md's own "Clean-up decision"
// section (Step 3.4), this harness was KEPT as a standing manual-
// verification tool, mirroring ComputeBlurValidation/GBufferValidation's own
// "never deleted after validation" precedent - see that report for the full
// `ask_questions` round-trip.

#include "EditorLayer.h"
#include "../Renderer/ComputeDescriptorSet.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/TextureArray2D.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <array>
#include <optional>

namespace gte {

namespace rg {
class RenderPassToggleRegistry;
} // namespace rg

class Renderer;

// IEditorLayer::TextureArrayValidationHandles - see EditorLayer.h's own
// definition (this class reuses that exact same struct as its own AddPass()
// return type directly, rather than defining a second, parallel one here -
// mirrors GBufferValidation.h's own identical precedent for
// GBufferValidationHandles).

class TextureArrayValidation {
public:
    TextureArrayValidation() = default;
    ~TextureArrayValidation();

    TextureArrayValidation(const TextureArrayValidation&) = delete;
    TextureArrayValidation& operator=(const TextureArrayValidation&) = delete;
    TextureArrayValidation(TextureArrayValidation&&) = delete;
    TextureArrayValidation& operator=(TextureArrayValidation&&) = delete;

    // Declares this frame's 5 passes into `builder` (Pass A - "
    // TextureArrayValidationFill", writing 4 distinct solid colors into the
    // pooled "ManualVerifyArray" TextureArray; Pass B x4 - "
    // TextureArrayValidationSliceN", each reading ONE layer back into its
    // own separately-registered 2D output) - lazily builds this object's own
    // Pipeline/ComputePipeline/RenderTexture outputs the first time this is
    // called (needs a live Renderer/VkDevice, so can't happen in the default
    // constructor).
    //
    // editor-core-separation-21 campaign, PHASE4's own precedent -
    // `toggleRegistry` (default nullptr) independently gates this pass pair
    // via RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled("TextureArrayValidation")
    // as an ADDITIONAL gate on top of the caller's own
    // ctx.showTextureArrayValidationOutput feature toggle.
    IEditorLayer::TextureArrayValidationHandles AddPass(
        rg::RenderGraphBuilder& builder, Renderer& renderer, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

    // Transitions all 4 per-layer outputs from the ComputeShaderWrite state
    // AddPass() above leaves them in to a real ShaderRead state - mirrors
    // GBufferValidation::FinalizeForSampling()'s own compute-half handling.
    // A safe no-op whenever AddPass() above was not actually called this
    // frame.
    void FinalizeForSampling(VkCommandBuffer cmd);

private:
    void EnsureInitialized(Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;

    // Pass A - fills the pooled TextureArray's 4 layers with distinct solid
    // colors. This pipeline's own descriptor set is rewritten every call
    // (RenderGraphResourcePool may legitimately hand back a different
    // physical TextureArray2D identity frame-to-frame if the pooling
    // behavior this harness exists to prove ever regressed - see
    // ComputeDescriptorSet.h's own header comment on why a descriptor set
    // must never be assumed stable).
    VkDescriptorSetLayout m_fillDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_fillPipeline;
    ComputeDescriptorSet m_fillDescriptorSet;

    // Pass B - reads ONE layer of the TextureArray back into its own 2D
    // output. One shared pipeline/layout, 4 independent descriptor
    // sets/output textures (one per layer).
    VkDescriptorSetLayout m_sliceDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_slicePipeline;
    std::array<ComputeDescriptorSet, 4> m_sliceDescriptorSets;
    std::array<std::optional<RenderTexture>, 4> m_layerOutputs;

    // Tracks whether this frame's AddPass() actually declared anything, so
    // FinalizeForSampling() above only emits a barrier when real work
    // happened - mirrors GBufferValidation's own m_copyWrittenThisFrame.
    bool m_writtenThisFrame = false;
};

} // namespace gte
