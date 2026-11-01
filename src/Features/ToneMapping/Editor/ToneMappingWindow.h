#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class ToneMappingFeature;

// The "ToneMapping" settings window.
class ToneMappingWindow final : public EditorWindow {
public:
    explicit ToneMappingWindow(ToneMappingFeature& feature)
        : EditorWindow("ToneMapping", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    ToneMappingFeature& m_feature;
};

} // namespace gte
