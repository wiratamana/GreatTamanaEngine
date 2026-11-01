#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class ShadowFeature;

// The "Shadow" settings window.
class ShadowWindow final : public EditorWindow {
public:
    explicit ShadowWindow(ShadowFeature& feature)
        : EditorWindow("Shadow", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    ShadowFeature& m_feature;
};

} // namespace gte
