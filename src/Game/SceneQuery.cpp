#include "SceneQuery.h"

#include "RenderSystem.h"

namespace gte {

void DrawScene(RenderSystem& renderSystem, Registry& registry, Renderer& renderer, const SceneDrawRequest& request)
{
    renderSystem.Draw(registry, renderer, request.viewProjection, request.frameDebuggerCapture, request.maxDrawCount,
        request.batchedEntities != nullptr ? *request.batchedEntities : std::unordered_set<Entity>{},
        request.pipelineOverride);
}

} // namespace gte
