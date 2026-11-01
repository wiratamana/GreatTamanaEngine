#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class GtaoFeature;

// The "Gtao" settings window.
class GtaoWindow final : public EditorWindow {
public:
    explicit GtaoWindow(GtaoFeature& feature)
        : EditorWindow("Gtao", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    GtaoFeature& m_feature;
};

} // namespace gte
