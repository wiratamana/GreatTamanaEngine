#pragma once

#include "../Math/Mat4.h"

#include <volk.h>

namespace gte {

class Renderer;

// Draws the Editor's "Scene" panel infinite ground grid (Unity-style
// procedural shader grid - see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) - a
// single full-screen-triangle draw call using a dedicated pipeline built
// from SceneGrid.vert/frag (PHASE2_GRID_SHADERS.md). Deliberately
// bypasses Renderer::CreatePipeline()/Renderer::CreateMesh()/
// Renderer::Submit() entirely, for the same reason AssetPreviewMesh
// already does (see that class's own header comment) - this pipeline
// needs alpha blending, depth-test-without-depth-write, a
// fragment-shader-written gl_FragDepth, and zero vertex input, none of
// which Renderer::CreatePipeline()'s fixed built-in vertex
// layouts/pipeline state support.
//
// Unlike AssetPreviewMesh, this class owns NO RenderTexture and never
// calls Renderer::RenderOffscreen()/BeginGraphPassRecording() itself -
// Draw() is called from INSIDE an already-open vkCmdBeginRendering
// bracket the caller (see PHASE4_RENDERGRAPH_INTEGRATION.md) owns, so the
// grid is recorded as one more draw call alongside the real scene
// geometry already drawn into the exact same color/depth attachments this
// frame - this is what makes it correctly depth-TESTED (never
// depth-WRITTEN - see Draw()'s own doc comment) against real scene
// objects already in front of it.
//
// Owns its VkPipeline/VkPipelineLayout for as long as they're needed - all
// released by Reset() (called by the destructor, and MUST also be called
// explicitly by ImGuiEditorLayer's destructor BEFORE
// ImGui_ImplVulkan_Shutdown(), mirroring AssetPreviewMesh/
// ComputeBlurValidation's own exact requirement - though in practice, since
// this class holds no ImGui descriptor of its own at all, plain
// destruction order is already safe; Reset() is still exposed explicitly
// for symmetry/consistency with those two siblings).
class SceneGridRenderer {
public:
    SceneGridRenderer() = default;
    ~SceneGridRenderer();

    SceneGridRenderer(const SceneGridRenderer&) = delete;
    SceneGridRenderer& operator=(const SceneGridRenderer&) = delete;
    SceneGridRenderer(SceneGridRenderer&&) = delete;
    SceneGridRenderer& operator=(SceneGridRenderer&&) = delete;

    // Records one full-screen-triangle draw call for the grid directly
    // against `cmd`, using `sceneViewProjection` (the SAME combined
    // view * projection matrix the real Scene-view geometry was just
    // rendered with this frame - e.g. EditorCamera::ViewProjection(aspect))
    // both to unproject each pixel's camera ray AND to re-derive its
    // correct depth. Lazily builds this object's own pipeline on first
    // call (needs a live Renderer/VkDevice - can't happen in the default
    // constructor).
    //
    // A safe no-op (draws nothing) whenever `sceneViewProjection` turns
    // out to be singular (Mat4::TryInverse() fails) - never asserts/
    // crashes on live, user-controlled camera state.
    //
    // The bound pipeline enables depth TESTING (VK_COMPARE_OP_LESS,
    // matching every other pipeline in this engine) but disables depth
    // WRITING, and alpha-blends against whatever is already in the color
    // attachment - see PHASE0_MASTER_STRATEGY.md's own "Depth handling"/
    // "Blending" design decisions for why. The caller is responsible for
    // having already set a viewport/scissor covering the render target
    // (already true by construction inside a RenderGraph pass - see
    // RenderGraph::ExecuteCompiledGraph()'s own per-pass vkCmdSetViewport/
    // vkCmdSetScissor call) - this method never sets either itself.
    void Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection);

    // Releases the pipeline/pipeline layout (if built) - waits for the GPU
    // to be idle first, same reasoning as AssetPreviewMesh::Reset(). Safe
    // to call repeatedly/on an already-empty instance.
    void Reset();

private:
    void EnsurePipeline(Renderer& renderer);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace gte
