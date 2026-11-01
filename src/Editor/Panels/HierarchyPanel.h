#pragma once

#include "../EditorWindow.h"

namespace gte {

// Entity tree of every Transform with drag-and-drop reparenting and a create menu.
class HierarchyPanel final : public EditorWindow {
public:
    HierarchyPanel() : EditorWindow("Hierarchy", WindowDock::Left) {}

protected:
    void OnGui(EditorFrame& frame) override;
    void OnHidden(EditorFrame& frame) override;
};

} // namespace gte
