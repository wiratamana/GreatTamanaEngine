#pragma once

#include "../Math/Mat4.h"
#include "../Renderer/Pipeline.h"

#include <volk.h>

#include <memory>

namespace gte {

class Renderer;

// Draws the Editor's "Scene" panel infinite ground grid (Unity-style
// procedural shader grid) - a single full-screen-triangle draw call using a
// dedicated pipeline built from SceneGrid.vert/frag.
//
// Unlike AssetPreviewMesh, this class owns NO RenderTexture and never calls
// Renderer::RenderOffscreen()/BeginGraphPassRecording() itself - Draw() is
// called from INSIDE an already-open vkCmdBeginRendering bracket the caller
// owns, so the grid is recorded as one more draw call alongside the real
// scene geometry already drawn into the exact same color/depth attachments
// this frame - this is what makes it correctly depth-TESTED (never
// depth-WRITTEN - see Draw()'s own doc comment) against real scene objects
// already in front of it.
//
// Built via Renderer's configurable pipeline API (Core/PipelineDesc.h) -
// alpha blending, depth-test-without-depth-write, zero vertex input, no
// extra descriptor sets.
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
    // rendered with this frame) both to unproject each pixel's camera ray
    // AND to re-derive its correct depth. Lazily builds this object's own
    // pipeline on first call (needs a live Renderer - can't happen in the
    // default constructor).
    //
    // A safe no-op (draws nothing) whenever `sceneViewProjection` turns out
    // to be singular (Mat4::TryInverse() fails) - never asserts/crashes on
    // live, user-controlled camera state.
    //
    // The bound pipeline enables depth TESTING but disables depth WRITING,
    // and alpha-blends against whatever is already in the color attachment.
    // The caller is responsible for having already set a viewport/scissor
    // covering the render target - this method never sets either itself.
    void Draw(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection);

    // Releases the pipeline (if built) - waits for the GPU to be idle
    // first. Safe to call repeatedly/on an already-empty instance.
    void Reset();

private:
    void EnsurePipeline(Renderer& renderer);

    Renderer* m_renderer = nullptr; // Non-owning - used only to call WaitForGpuIdle() in Reset().
    std::unique_ptr<Pipeline> m_pipeline;
};

} // namespace gte
