#pragma once

#include "../CameraFrustumGizmo.h"
#include "../../Core/GraphicsHandles.h"
#include "../../ECS/Entity.h"

namespace gte {

struct EditorContext;
class EditorCamera;
class Game;
class Registry;
class Renderer;

// Scene view: own render target, orbit camera, transform gizmo and asset drag-and-drop spawning. Once per frame.
// `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set, used by the spawn path.
void BuildScenePanel(
    Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera, DescriptorSetLayoutHandle sceneServicesSetLayout);

// Game view width / height; 16/9 while the Game view has no size yet.
float GameViewAspect(const EditorContext& ctx);

// World pose and lens of a Camera entity. An entity without Camera gives an invalid input; callers check FrustumInputIsValid.
FrustumInput MakeSceneCameraFrustum(Registry& registry, Entity entity, float aspect);

} // namespace gte
