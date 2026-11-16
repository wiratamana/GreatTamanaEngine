#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class GrassFeature;

// The "Grass" settings window.
class GrassWindow final : public EditorWindow {
public:
    explicit GrassWindow(GrassFeature& feature)
        : EditorWindow("Grass", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    GrassFeature& m_feature;
};

} // namespace gte
