#pragma once

// State shared by the Water panel and the Game and Scene view gizmos. The water world only changes CPU data here;
// the host turns it into GPU work in its own Tick, so the UI may call it directly.

#include "../../../Math/Vec2.h"
#include "../World/WaterControl.h"

#include <cstdint>
#include <string>

namespace gte {

struct WaterUiBridge {
    water::IWaterWorld* world = nullptr; // Set by the host; null until the editor has built the world.

    std::uint32_t selectedId = 0;
    int selectedPoint = -1;
    bool gizmos = true;     // Draw and edit the handles in the views.
    bool panelVisible = false; // The Water window shows its content; the Game view draws handles only then.
    bool handleHot = false; // The cursor is on a handle or one is dragged: the camera must not react.

    bool dragging = false;
    std::uint32_t dragBody = 0;
    int dragPoint = -1;
    double dragPlaneY = 0.0;
    Vec2 dragRect;          // Top-left of the view that owns the drag, so a second view ignores it.

    // What the gizmo saw on its last pass, shown in the panel so an AI can debug the mouse path.
    int debugHandles = 0;
    int debugPick = -1;
    bool debugFree = false;
    bool debugHovered = false;
    Vec2 debugMouse;

    std::string status;     // Outcome of the last edit, shown in the panel.
    bool statusIsError = false;
};

} // namespace gte
