#pragma once

#include "../../Core/GraphicsHandles.h"

namespace gte {

class Game;
class Renderer;
struct EditorContext;

// Entity tree of every Transform, with drag-and-drop reparenting and a "Create 3D Object" menu. Once per frame.
// `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set, used by spawn paths.
void BuildHierarchyPanel(Game& game, Renderer& renderer, EditorContext& ctx, DescriptorSetLayoutHandle sceneServicesSetLayout);

} // namespace gte
