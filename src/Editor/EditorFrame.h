#pragma once

#include "../Core/GraphicsHandles.h"
#include "../Core/Plugins/RenderFeatureDebugEntry.h"

#include <vector>

namespace gte {

struct EditorContext;
class Game;
class Renderer;
class RenderTexture;
class RenderFeatureCompositor;

namespace rg {
class RenderGraph;
class RenderPassToggleRegistry;
class RenderPassBlackboard;
} // namespace rg

// Everything the editor layer hands to every window once per frame. Valid only during that frame.
struct EditorFrame {
    EditorContext& ctx;
    Game& game;
    Renderer& renderer;
    const rg::RenderGraph& renderGraph;
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries;
    rg::RenderPassToggleRegistry& renderPassToggleRegistry;
    RenderFeatureCompositor* renderFeatureCompositor; // Null when no render feature is loaded.
    const rg::RenderPassBlackboard& offscreenBlackboard;
    DescriptorSetLayoutHandle sceneServicesSetLayout;
    RenderTexture& gameView;
    RenderTexture* gameViewComposited; // Null on frames where the Game view was not rendered.
};

} // namespace gte
