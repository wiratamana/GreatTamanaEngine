#include "EngineCommandDispatch.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"

namespace gte {

EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, const EngineCommandRequest& request)
{
    EngineCommandResult result;
    result.kind = request.kind;
    switch (request.kind) {
    case EngineCommandKind::InstantiatePrimitive: {
        const InstantiatePrimitiveCommand& cmd = request.instantiatePrimitive;
        result.instantiatePrimitive = game.InstantiatePrimitive(
            renderer, cmd.shape, cmd.requestedName, cmd.worldPosition, cmd.hasParent, cmd.parentName);
        break;
    }
    case EngineCommandKind::DeleteEntity: {
        result.deleteEntity = game.DeleteEntityByName(request.deleteEntity.name);
        break;
    }
    }
    return result;
}

} // namespace gte
