#pragma once

#include "../../../Editor/EditorWindow.h"

namespace gte {

class AtmosphereFeature;

// The "Atmosphere" settings window.
class AtmosphereWindow final : public EditorWindow {
public:
    explicit AtmosphereWindow(AtmosphereFeature& feature)
        : EditorWindow("Atmosphere", WindowDock::Bottom)
        , m_feature(feature)
    {
    }

protected:
    void OnGui(EditorFrame& frame) override;

private:
    AtmosphereFeature& m_feature;
};

} // namespace gte
