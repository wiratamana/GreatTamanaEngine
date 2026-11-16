#pragma once

// The Terrain window: world open/close, live preview, every setting as a slider, camera mode, asset save/load,
// bounding-box overlay and live stats. It talks to the world only through TerrainUiBridge.

#include "TerrainUiBridge.h"
#include "../EditorWindow.h"

#include <array>
#include <optional>
#include <string>

namespace gte {

class TerrainPanel final : public EditorWindow {
public:
    TerrainPanel() : EditorWindow("Terrain", WindowDock::Right, /*openByDefault=*/false) {}

protected:
    void OnGui(EditorFrame& frame) override;

private:
    void DrawWorld(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap);
    void DrawCamera(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap);
    void DrawParameters();
    void DrawRuntime(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap);
    void DrawAsset(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap);
    void DrawDebug(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap);
    void DrawStats(const TerrainUiSnapshot& snap) const;
    void SyncWorking(const TerrainUiSnapshot& snap);
    void CommitEdits(TerrainUiBridge& bridge, const TerrainUiSnapshot& snap);
    void MarkEdited();

    terrain::TerrainSettings m_working;          // What the sliders show.
    terrain::TerrainRuntimeOptions m_runtime;
    std::optional<terrain::TerrainSettings> m_sent; // Last settings sent to the world.
    bool m_haveWorking = false;
    bool m_editPending = false;
    bool m_dragging = false;
    double m_secondsSinceEdit = 0.0;
    int m_framesSinceSent = 0;

    int m_profileIndex = 1;                      // Lean.
    bool m_budget = true;
    bool m_water = true;
    std::array<char, 512> m_path{};
    bool m_pathInitialised = false;
};

} // namespace gte
