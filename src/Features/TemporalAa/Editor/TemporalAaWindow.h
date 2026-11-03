#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class TemporalAaFeature;

// The "TemporalAa" settings window.
class TemporalAaWindow final : public EditorWindow {
public:
    explicit TemporalAaWindow(TemporalAaFeature& feature)
        : EditorWindow("TemporalAa", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    TemporalAaFeature& m_feature;
};

} // namespace gte
