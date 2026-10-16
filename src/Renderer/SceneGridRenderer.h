#pragma once

#include "../Math/Mat4.h"
#include "RenderGraph/RecordingContext.h"

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

    // Records the grid draw into `cmd` (viewport and scissor must already cover the target). Builds the pipeline
    // on first call; draws nothing if `sceneViewProjection` is singular or the pipeline cannot be built.
    void Draw(Renderer& renderer, const rg::RecordingContext& cmd, const Mat4& sceneViewProjection);

    // Releases the pipeline after the GPU is idle. Safe to call repeatedly.
    void Reset();

private:
    bool EnsurePipeline(Renderer& renderer);

    Renderer* m_renderer = nullptr; // Non-owning; used only to wait for the GPU in Reset().
    std::unique_ptr<Pipeline> m_pipeline;
    bool m_buildFailed = false;
};

} // namespace gte
