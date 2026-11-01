#pragma once

#include "../EditorWindow.h"

namespace gte {

// Render feature priority and enable authoring.
class RenderFeaturesPanel final : public EditorWindow {
public:
    RenderFeaturesPanel() : EditorWindow("Render Features", WindowDock::Floating, false, 520.0f, 420.0f) {}

protected:
    void OnGui(EditorFrame& frame) override;
};

} // namespace gte
