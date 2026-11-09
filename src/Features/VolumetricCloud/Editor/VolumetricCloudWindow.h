#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class VolumetricCloudFeature;

// The "VolumetricCloud" settings window.
class VolumetricCloudWindow final : public EditorWindow {
public:
    explicit VolumetricCloudWindow(VolumetricCloudFeature& feature)
        : EditorWindow("VolumetricCloud", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    VolumetricCloudFeature& m_feature;
};

} // namespace gte
