#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"

namespace gte {

class Renderer;

// Sits on top of Window/Renderer and has no direct knowledge of SDL.
// All engine/game logic lives here (and in whatever this grows into).
class Game {
public:
    Game() = default;

    // Called once per discrete/translated event, before Update() runs for
    // that frame. Use this for one-shot reactions (a key just pressed,
    // window resized, etc) - anything continuous ("is this key held") should
    // be read from the InputState passed to Update() instead.
    void OnEvent(const Event& event);

    void Update(double deltaSeconds, const InputState& input);
    void Render(Renderer& renderer);
};

} // namespace gte
