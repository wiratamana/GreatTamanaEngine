#pragma once

// Phase 7 (COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md, part 7
// of the wider COMPUTE_SHADER_MASTER_STRATEGY_v2.md compute-shader
// campaign) - "Real tests: prove it works", the TEXTURE-side half. The
// buffer-side half (a GPU frustum-culling workload) is the companion
// GPU_DRIVEN_RENDERING_COMPUTE_INDIRECT_STRATEGY_v1.md document's own
// responsibility and is not implemented here.
//
// This class owns everything a real, SHIPPED compute-shader pass needs
// end-to-end: a ComputePipeline (Phase 2), a descriptor-set-layout +
// descriptor set (Phase 3), and a dedicated, persistent, storage-capable
// `RenderTexture` (Phase 1's `RWTexture`) - and knows how to declare
// itself as a genuine RenderGraph pass (Phase 6) every frame, reading the
// Editor's "Scene" view (already rendered by an earlier graphics pass in
// the SAME offscreen RenderGraph::Execute() call - see Application::Run())
// as a plain `Texture`, and writing its own `RWTexture` output.
//
// This is a deliberately SIMPLE, non-production box blur (see
// Shaders/BoxBlur.comp's own header comment) - the point is proving the
// plumbing (a compute pass reading a texture another pass just wrote,
// entirely synchronized by the render graph's own automatic barrier
// planner - see RENDERGRAPH_FUTURE_TODO_DELIBERATELY_NOT_IMPLEMENTED.md,
// Section B.2), not shipping a real post-process feature - see this
// phase's own strategy document's "What We Will NOT Do".
//
// Owned by ImGuiEditorLayer (src/Editor/ImGuiEditorLayer.cpp), alongside
// its own m_gameView/m_sceneView RenderTextures - exposed to Application
// purely through two new IEditorLayer methods (AddBlurValidationPass()/
// FinalizeBlurValidationForSampling(), see EditorLayer.h), mirroring
// GameViewTarget()/SceneViewTarget()'s own "Application only ever sees an
// opaque RenderTexture*/handle, never Editor internals" boundary. A small,
// permanent, clearly-labeled "Show Compute Blur (debug)" checkbox in the
// "Scene" panel's own toolbar (see Panels/ScenePanel.cpp) toggles whether
// this pass is even declared at all each frame - per this phase's own
// "Step 5: Their Role", this is deliberately KEPT (not deleted after
// validation) as a small, permanent Editor debug tool, the same
// discipline the Bone Viewer already established.
//
// `blurredOutput`'s color image is created at an EXPLICIT
// VK_FORMAT_R8G8B8A8_UNORM (never the default/Renderer::ColorFormat()) -
// see this class's own .cpp for the full reasoning (this texture has no
// pipeline-sharing requirement with the swapchain/Game/Scene views at all,
// so there is no reason to inherit the swapchain's own uncertain
// storage-image-format support - see
// COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md's own Step 6).

#include "../Renderer/ComputeDescriptorSet.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <optional>

namespace gte {

class Renderer;

class ComputeBlurValidation {
public:
    ComputeBlurValidation() = default;
    ~ComputeBlurValidation();

    ComputeBlurValidation(const ComputeBlurValidation&) = delete;
    ComputeBlurValidation& operator=(const ComputeBlurValidation&) = delete;
    ComputeBlurValidation(ComputeBlurValidation&&) = delete;
    ComputeBlurValidation& operator=(ComputeBlurValidation&&) = delete;

    // Declares this frame's blur pass into `builder`: reads
    // `sceneViewHandle` (this call's own already-imported, just-rendered
    // Scene view texture, declared ShaderRead - a plain `Texture` read,
    // exactly like a fragment shader sampling a MaterialTexture today) and
    // writes this object's own persistent blurredOutput RenderTexture
    // (imported fresh, every call, per Phase 1 v2's own scope note - a
    // storage-capable RWTexture can only ever be an externally-owned,
    // persistent resource, never a RenderGraphBuilder::CreateTexture()-
    // requested transient one), resized to match `sceneExtent` first if
    // needed. `sceneViewSampler` must be the SAME RenderTexture's own
    // Sampler() the caller resolved `sceneViewHandle` from - passed in
    // directly rather than resolved via PassContext::resolveTexture(),
    // since an IMPORTED texture's resolved sampler is always
    // VK_NULL_HANDLE (see RenderGraph.cpp's EnsureTextureResolved() -
    // TextureImportInfo carries no sampler of its own).
    //
    // Lazily builds this object's own ComputePipeline/descriptor-set-
    // layout/descriptor-set/RenderTexture the first time this is called
    // (needs a live Renderer/VkDevice, so can't happen in the default
    // constructor above). `sceneExtent` must be non-zero in both
    // dimensions - the caller is expected to have already checked this
    // (mirrors ImGuiEditorLayer::GameViewTarget()/SceneViewTarget()'s own
    // "only resize/act when avail.x/avail.y >= 1" guard).
    //
    // Returns the blurred output's TextureHandle - the CALLER must add it
    // to this call's own finalOutputs root set, or this pass's write will
    // be silently culled the next time RenderGraphCompiler::Compile() runs
    // (see COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md's own Step 6 -
    // the same "a resource write with no in-graph reader/root is dead
    // code" constraint, here applied to a texture write instead of a
    // buffer one).
    rg::TextureHandle AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer, rg::TextureHandle sceneViewHandle,
        VkSampler sceneViewSampler, VkExtent2D sceneExtent);

    // Transitions the blurred output texture from the ComputeShaderWrite
    // state AddPass() above leaves it in (VK_IMAGE_LAYOUT_GENERAL) to a
    // real ShaderRead state, ready for Dear ImGui to sample it directly
    // (mirrors RenderPasses.h's own FinalizeRenderTextureForExternalSampling()
    // for the Game/Scene views, applied here against ComputeShaderWrite as
    // the "previous" access instead of ColorAttachmentWrite). Must be
    // called against the SAME command buffer the offscreen
    // RenderGraph::Execute() call just recorded into, AFTER that call
    // returns and BEFORE that command buffer is ended/submitted - see
    // Application::Run(). A safe no-op whenever AddPass() above was not
    // actually called this frame (the debug toggle is off, or the "Scene"
    // panel wasn't visible) - tracked internally, never assumed from the
    // caller's own condition.
    void FinalizeForSampling(VkCommandBuffer cmd);

    // The blurred output RenderTexture itself, or nullptr before AddPass()
    // has ever been called - used by the caller (ImGuiEditorLayer) to
    // (re)create its own ImGui descriptor for displaying it via
    // ImGui::Image(), mirroring how it already does this for m_sceneView/
    // m_gameView.
    RenderTexture* OutputTexture() noexcept { return m_blurredOutput.has_value() ? &m_blurredOutput.value() : nullptr; }

private:
    void EnsureInitialized(Renderer& renderer, VkExtent2D initialExtent);

    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> m_pipeline;
    ComputeDescriptorSet m_descriptorSet;
    std::optional<RenderTexture> m_blurredOutput;

    // Set true at the end of a real AddPass() call, and consumed (reset to
    // false) by FinalizeForSampling() above - see that method's own doc
    // comment for why this is tracked internally rather than trusted to
    // match the caller's own enable/visibility condition every time.
    bool m_writtenThisFrame = false;
};

} // namespace gte
