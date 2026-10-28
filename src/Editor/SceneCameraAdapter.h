#pragma once

// Scene camera control for the HTTP bridge, backed by the Scene panel's last camera overlay frame.

#include "SceneCameraControl.h"

namespace gte {

class Registry;
class Renderer;
class RenderTexture;
class EditorCamera;
struct EditorContext;

class SceneCameraAdapter final : public ISceneCameraControl {
public:
    // Stores non-owning pointers; they stay valid for the whole editor frame, when HTTP commands run.
    void BeginFrame(Renderer& renderer, EditorContext& ctx, Registry& registry, RenderTexture& sceneView,
        EditorCamera& camera) noexcept;

    SceneCameraReport Report() const override;
    SceneCameraResult Select(const std::string& entityName) override;
    SceneCameraPickResult Pick(float x, float y) override;
    SceneCameraResult SetPose(const SceneCameraPose& pose) override;
    std::optional<ClusterViewerImage> CaptureImage(bool overlay) override;

private:
    Renderer* m_renderer = nullptr;
    EditorContext* m_ctx = nullptr;
    Registry* m_registry = nullptr;
    RenderTexture* m_sceneView = nullptr;
    EditorCamera* m_camera = nullptr;
};

} // namespace gte
