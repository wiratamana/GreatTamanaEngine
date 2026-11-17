#pragma once

// The Water window: global look and performance settings, the body list (ocean, lakes, rivers), per-body carve and
// look, and file save/load. Handles in the Game and Scene views edit the same bodies (see WaterGizmoOverlay).

#include "../../../Editor/EditorWindow.h"

#include <array>

namespace gte {

struct WaterUiBridge;
struct EditorContext;

class WaterPanel final : public EditorWindow {
public:
    WaterPanel() : EditorWindow("Water", WindowDock::Right, /*openByDefault=*/false) {}

protected:
    void OnGui(EditorFrame& frame) override;

private:
    void DrawStatus(WaterUiBridge& ui);
    void DrawSettings(WaterUiBridge& ui);
    void DrawBodyList(WaterUiBridge& ui, EditorContext& ctx);
    void DrawSelectedBody(WaterUiBridge& ui);
    void DrawFile(WaterUiBridge& ui);
    void AddBodyHere(WaterUiBridge& ui, EditorContext& ctx, bool river);

    std::array<char, 512> m_path{};
    bool m_pathInitialised = false;
    std::array<char, 128> m_name{};
    std::uint32_t m_nameFor = 0;
};

} // namespace gte
