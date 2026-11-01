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

// Result of SetSceneGameSplit(). `error` is empty when `applied` is true.
struct SceneGameSplitResult {
    bool applied = false;
    bool split = false; // Game and Scene sit in separate dock nodes.
    const char* error = "";
};

// Docks "Game" beside "Scene" (enabled) or back as a tab of "Scene". Call between NewFrame() and BuildUI().
SceneGameSplitResult SetSceneGameSplit(const EditorContext& ctx, bool enabled);

} // namespace gte
