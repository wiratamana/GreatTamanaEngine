#pragma once

#include "../EditorWindow.h"
#include "../LogPanelData.h"

namespace gte {

// Scrolling, filterable, colored list of every entry the Logger retains.
class LogPanel final : public EditorWindow {
public:
    LogPanel() : EditorWindow("Log", WindowDock::Bottom) {}

protected:
    void OnGui(EditorFrame& frame) override;

private:
    LogPanelFilterState m_filterState;
};

} // namespace gte
