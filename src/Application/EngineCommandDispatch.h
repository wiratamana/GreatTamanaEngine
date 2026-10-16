#pragma once

#include "../Core/GraphicsHandles.h"

// Main-thread dispatcher that executes one pending EngineCommandRequest against Game/Renderer.

#include "EngineCommandBridge.h"
#include "../Core/EditorCapabilities.h"

namespace gte {

class Game;
class Renderer;

// Runs one request on the live Game/Renderer and returns the result. Never throws.
// Null capabilities mean the host has none: those commands answer "not available".
// `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set.
EngineCommandResult ExecuteEngineCommand(Game& game, Renderer& renderer, ISceneIOCapability* sceneIOCapability,
    IHotReloadDebugCapability* hotReloadDebugCapability, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const EngineCommandRequest& request);

} // namespace gte
