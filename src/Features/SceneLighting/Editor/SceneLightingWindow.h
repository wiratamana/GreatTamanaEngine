#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class SceneLightingFeature;

// The "SceneLighting" settings window.
class SceneLightingWindow final : public EditorWindow {
public:
    explicit SceneLightingWindow(SceneLightingFeature& feature)
        : EditorWindow("SceneLighting", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    SceneLightingFeature& m_feature;
};

} // namespace gte
