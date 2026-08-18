#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "../Renderer/Mesh.h"
#include "../Renderer/Pipeline.h"

#include <optional>

namespace gte {

class Renderer;

// Sits on top of Window/Renderer and has no direct knowledge of SDL, Vulkan
// beyond the Renderer abstraction (Pipeline/Mesh included - Game never
// issues a raw Vulkan call itself, it only owns these RAII objects and hands
// them to Renderer::Submit()), or the (optional) Editor module - Game has
// no idea an Editor even exists. All engine/game logic lives here (and in
// whatever this grows into).
class Game {
public:
    Game() = default;

    // Called once per discrete/translated event, before Update() runs for
    // that frame. Use this for one-shot reactions (a key just pressed,
    // window resized, etc) - anything continuous ("is this key held") should
    // be read from the InputState passed to Update() instead.
    void OnEvent(const Event& event);

    void Update(double deltaSeconds, const InputState& input);

    // Sets the clear color and queues this frame's draw calls via
    // Renderer::Submit(). Deliberately does NOT call Renderer::Present()/
    // RenderOffscreen() itself - *where* this frame ends up (the swapchain,
    // fullscreen, or an Editor's off-screen "Game view" RenderTexture) is
    // decided by Application (the composition root), not by Game. See
    // Application::Run().
    void Render(Renderer& renderer);

private:
    // Hardcoded "hello triangle" milestone - proves shader -> Pipeline ->
    // Mesh -> Renderer::Submit() -> draw works end-to-end. Lazily built on
    // the first Render() call (needs a Renderer to build GPU resources
    // through - see Renderer::CreatePipeline()/CreateMesh()); std::optional
    // since neither type is default-constructible. Will be replaced by a
    // real scene/material system once there's more than one hardcoded thing
    // to draw.
    std::optional<Pipeline> m_trianglePipeline;
    std::optional<Mesh> m_triangleMesh;
};

} // namespace gte
