#pragma once

#include "../Core/GraphicsHandles.h"

namespace gte {

struct EditorContext;
class Game;
class Renderer;
class IHotReloadDebugCapability;

// Builds the full-viewport dockspace and menu bar, and handles the Save/Open shortcuts. Once per frame.
// `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set.
void BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer,
    IHotReloadDebugCapability* hotReloadDebugCapability, DescriptorSetLayoutHandle sceneServicesSetLayout);

// Focuses the ImGui window with this exact name. False when no such window exists yet.
bool FindAndFocusEditorWindow(const char* panelName);

} // namespace gte
