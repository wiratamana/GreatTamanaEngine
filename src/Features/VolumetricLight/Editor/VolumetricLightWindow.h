#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class VolumetricLightFeature;

// The "VolumetricLight" settings window.
class VolumetricLightWindow final : public EditorWindow {
public:
    explicit VolumetricLightWindow(VolumetricLightFeature& feature)
        : EditorWindow("VolumetricLight", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    VolumetricLightFeature& m_feature;
};

} // namespace gte
