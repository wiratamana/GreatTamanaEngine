#pragma once

// Frustum of a Scene Camera entity, as drawn by the Scene panel and the cluster viewer. No UI types.

#include "CameraFrustumGizmo.h"
#include "../ECS/Entity.h"

namespace gte {

struct EditorContext;
class Registry;

// Game view width / height; 16/9 while the Game view has no size yet.
float GameViewAspect(const EditorContext& ctx);

// World pose and lens of a Camera entity. An entity without Camera gives an invalid input; callers check FrustumInputIsValid.
FrustumInput MakeSceneCameraFrustum(Registry& registry, Entity entity, float aspect);

} // namespace gte
