#pragma once

namespace gte {

class Renderer;

// Sits on top of Window/Renderer and has no direct knowledge of SDL.
// All engine/game logic lives here (and in whatever this grows into).
class Game {
public:
    Game() = default;

    void Update(double deltaSeconds);
    void Render(Renderer& renderer);
};

} // namespace gte
