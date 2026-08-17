#include "Game.h"

#include "../Renderer/Renderer.h"

namespace gte {

void Game::Update(double /*deltaSeconds*/)
{
    // Game/simulation logic goes here.
}

void Game::Render(Renderer& renderer)
{
    // Placeholder: clear to a dark blue-grey each frame.
    renderer.Clear(20, 20, 30, 255);

    // Draw calls go here.

    renderer.Present();
}

} // namespace gte
