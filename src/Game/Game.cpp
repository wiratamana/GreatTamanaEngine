#include "Game.h"

#include "../Renderer/Renderer.h"

namespace gte {

void Game::OnEvent(const Event& /*event*/)
{
    // Discrete/one-shot event handling goes here (react to a single key
    // press, window resize, etc). event.type tells you which alternative of
    // event.data is active - see Event.h.
}

void Game::Update(double /*deltaSeconds*/, const InputState& /*input*/)
{
    // Game/simulation logic goes here. Poll `input` for continuous state,
    // e.g. `if (input.IsKeyDown(KeyCode::W)) { ... }` for held-key movement.
}

void Game::Render(Renderer& renderer)
{
    // Placeholder: clear to a dark blue-grey each frame.
    renderer.Clear(20, 20, 30, 255);

    // Draw calls go here. Application decides afterwards whether this frame
    // is presented straight to the swapchain or rendered into an Editor's
    // off-screen "Game view" texture - see Application::Run().
}

} // namespace gte
