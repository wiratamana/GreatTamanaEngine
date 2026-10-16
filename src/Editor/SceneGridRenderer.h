#pragma once

#include "../Math/Mat4.h"
#include "../Renderer/RenderGraph/RecordingContext.h"

#include <memory>

namespace gte {

class Pipeline;
class Renderer;

// Draws the Scene panel's infinite ground grid: one full-screen-triangle draw using its own pipeline.
// It is recorded inside the already-open Scene view pass, so it is depth-tested but never depth-written.
class SceneGridRenderer {
public:
    SceneGridRenderer() = default;
    ~SceneGridRenderer();

    SceneGridRenderer(const SceneGridRenderer&) = delete;
    SceneGridRenderer& operator=(const SceneGridRenderer&) = delete;
    SceneGridRenderer(SceneGridRenderer&&) = delete;
    SceneGridRenderer& operator=(SceneGridRenderer&&) = delete;

    // Records the grid draw into `cmd`. `sceneViewProjection` must be the matrix the scene was drawn with.
    // Builds the pipeline on first call. Draws nothing when the matrix is singular.
    // The caller must already have set a viewport and scissor covering the target.
    void Draw(Renderer& renderer, const rg::RecordingContext& cmd, const Mat4& sceneViewProjection);

    // Releases the pipeline after the GPU is idle. Safe to call repeatedly.
    void Reset();

private:
    void EnsurePipeline(Renderer& renderer);

    Renderer* m_renderer = nullptr; // Non-owning; used only to wait for the GPU in Reset().
    std::unique_ptr<Pipeline> m_pipeline;
};

} // namespace gte
