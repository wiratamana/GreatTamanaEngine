#pragma once

#include "../EditorWindow.h"

namespace gte {

// GPU memory totals, live resources, heap budgets and descriptor pools. Reads the renderer only.
class MemoryPanel final : public EditorWindow {
public:
    MemoryPanel() : EditorWindow("Memory", WindowDock::Bottom) {}

protected:
    void OnGui(EditorFrame& frame) override;
};

} // namespace gte
