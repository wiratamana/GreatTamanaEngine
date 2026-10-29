#include "SceneQuery.h"

#include "RenderSystem.h"

namespace gte {

namespace {
const EntityMarkSet kNoExcludedEntities;
} // namespace

const EntityMarkSet& ExcludedEntitiesOf(const SceneDrawRequest& request) noexcept
{
    return request.batchedEntities != nullptr ? *request.batchedEntities : kNoExcludedEntities;
}

void DrawScene(RenderSystem& renderSystem, Registry& registry, Renderer& renderer, const SceneDrawRequest& request)
{
    renderSystem.Draw(registry, renderer, request.viewProjection, request.frameDebuggerCapture, request.maxDrawCount,
        ExcludedEntitiesOf(request), request.pipelineOverrideSet, request.sceneServicesSet, /*cmd=*/nullptr,
        request.cull);
}

} // namespace gte
