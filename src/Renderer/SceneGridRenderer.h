#pragma once

#include "../Math/Mat4.h"
#include "RenderGraph/RecordingContext.h"

#include <memory>

namespace gte {

class Pipeline;
class Renderer;

// Draws the Scene panel's infinite ground grid: one full-screen-triangle draw using its own pipeline.
// It is recorded inside the already-open secondary view pass, so it is depth-tested but never depth-written.
class SceneGridRenderer {
public:
    // The Renderer must outlive this object; the destructor idles the GPU before the pipeline is freed.
    explicit SceneGridRenderer(Renderer& renderer) noexcept
        : m_renderer(renderer)
    {
    }
    ~SceneGridRenderer();

    SceneGridRenderer(const SceneGridRenderer&) = delete;
    SceneGridRenderer& operator=(const SceneGridRenderer&) = delete;
    SceneGridRenderer(SceneGridRenderer&&) = delete;
    SceneGridRenderer& operator=(SceneGridRenderer&&) = delete;

    // Records the grid draw into `cmd` (viewport and scissor must already cover the target). Builds the pipeline
    // on first call; draws nothing if `sceneViewProjection` is singular or the pipeline cannot be built.
    void Draw(const rg::RecordingContext& cmd, const Mat4& sceneViewProjection);

private:
    bool EnsurePipeline();

    Renderer& m_renderer;
    std::unique_ptr<Pipeline> m_pipeline;
    bool m_buildFailed = false;
};

} // namespace gte
