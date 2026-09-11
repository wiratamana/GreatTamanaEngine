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
    // network-impl-5 campaign - neither of these two touches `renderer` at
    // all (see PHASE2's own Testability note) - `renderer` stays
    // unreferenced in these two branches, exactly mirroring
    // DeleteEntity's own branch immediately above, which also never
    // touches it.
    case EngineCommandKind::SetEntityTrs: {
        result.setEntityTrs = game.SetEntityTrs(request.setEntityTrs);
        break;
    }
    case EngineCommandKind::InstantiateLight: {
        result.instantiateLight = game.InstantiateLight(request.instantiateLight);
        break;
    }
    }
    return result;
}

} // namespace gte
