#pragma once

#include "../EditorWindow.h"

namespace gte {

// The Game view. Writes its content size to ctx.desiredExtent; the editor layer sizes the view texture to it next frame.
class GamePanel final : public EditorWindow {
public:
    GamePanel() : EditorWindow("Game", WindowDock::Center) {}

protected:
    int ExtraFlags() const override;
    void OnGui(EditorFrame& frame) override;
};

} // namespace gte
