#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class SsgiFeature;

// The "Ssgi" settings window.
class SsgiWindow final : public EditorWindow {
public:
    explicit SsgiWindow(SsgiFeature& feature)
        : EditorWindow("Ssgi", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    SsgiFeature& m_feature;
};

} // namespace gte
