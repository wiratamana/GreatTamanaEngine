#pragma once

#include "../../Core/GraphicsHandles.h"
#include "../../Core/Plugins/RenderFeatureDebugEntry.h"

#include <vector>

namespace gte {

struct EditorContext;
class EditorCamera;
class Game;
class Renderer;

// Scene view: own render target, orbit camera, gizmo and asset drop spawning. Once per frame.
// `sceneServicesSetLayout` serves the spawn path; `renderFeatureEntries` tells whether a feature is off here.
void BuildScenePanel(Game& game, Renderer& renderer, EditorContext& ctx, EditorCamera& camera,
    DescriptorSetLayoutHandle sceneServicesSetLayout, const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries);

} // namespace gte
