#pragma once

#include "../LogPanelData.h"

#include <cstdint>

namespace gte {

struct EditorContext;

// The "Log" panel (task_manager/logger-1 campaign, PHASE4) - a Unity
// Console-style scrolling, filterable, colored list of every entry
// Editor/Logger.h currently retains. A small, stateful, non-polymorphic
// class (mirrors JobsPanel.h exactly), called by name from
// ImGuiEditorLayer::BuildUI() - no IEditorPanel interface introduced (see
// AGENTS.md, "Editor Module Structure").
class LogPanel {
public:
    void Build(EditorContext& ctx);

private:
    LogPanelFilterState m_filterState;
};

} // namespace gte
