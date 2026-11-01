#pragma once

#include "../EditorWindow.h"

namespace gte {

class EditorCamera;

// Scene view: own render target, camera, gizmo and asset drop spawning.
class ScenePanel final : public EditorWindow {
public:
    explicit ScenePanel(EditorCamera& camera) : EditorWindow("Scene", WindowDock::Center), m_camera(camera) {}

protected:
    int ExtraFlags() const override;
    void OnFrame(EditorFrame& frame) override;
    void OnGui(EditorFrame& frame) override;

private:
    EditorCamera& m_camera;
};

} // namespace gte
