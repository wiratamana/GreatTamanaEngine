#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "ECS/Registry.h"
#include "RenderSystem.h"

namespace gte {

class Renderer;

// Sits on top of Window/Renderer and has no direct knowledge of SDL, or of
// Vulkan beyond the Renderer abstraction. Owns the ECS World (m_registry)
// plus RenderSystem (src/Game/RenderSystem.h - the one thing allowed to
// depend on both ECS and Renderer) instead of holding a hardcoded
// Pipeline/Mesh pair directly: Game never touches a Mesh/Pipeline/Vulkan
// handle itself anymore, it only ever creates entities/components and calls
// RenderSystem::Draw(). All engine/game logic lives here (and in whatever
// this grows into).
class Game {
public:
    Game() = default;

    // Called once per discrete/translated event, before Update() runs for
    // that frame. Use this for one-shot reactions (a key just pressed,
    // window resized, etc) - anything continuous ("is this key held") should
    // be read from the InputState passed to Update() instead.
    void OnEvent(const Event& event);

    void Update(double deltaSeconds, const InputState& input);

    // Sets the clear color and, via RenderSystem::Draw(), queues this
    // frame's draw calls for every entity that has a MeshRenderer. Does NOT
    // call Renderer::Present()/RenderOffscreen() itself - *where* this frame
    // ends up (the swapchain, fullscreen, or an Editor's off-screen "Game
    // view" RenderTexture) is decided by Application (the composition
    // root), not by Game. See Application::Run().
    void Render(Renderer& renderer);

private:
    // Lazily builds the demo scene - three entities sharing one triangle
    // Mesh/Pipeline, spaced left/center/right purely via Transform - on the
    // first Render() call (needs a Renderer to build GPU resources
    // through). Proves the ECS -> RenderSystem -> Renderer pipeline end to
    // end: multiple entities, one shared mesh/pipeline, independently
    // positioned via push-constant model matrices. Will be replaced by a
    // real scene/asset-loading system once there's more than a hardcoded
    // demo scene.
    void EnsureDemoSceneBuilt(Renderer& renderer);

    Registry m_registry;
    RenderSystem m_renderSystem;
    bool m_demoSceneBuilt = false;
};

} // namespace gte
