#pragma once

#include "../../Core/GraphicsHandles.h"

namespace gte {

struct EditorContext;
class EditorCamera;
class Game;
class Renderer;

// Scene view: own render target, orbit camera, transform gizmo and asset drag-and-drop spawning. Once per frame.
// `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set, used by the spawn path.
void BuildScenePanel(
    Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera, DescriptorSetLayoutHandle sceneServicesSetLayout);

} // namespace gte
