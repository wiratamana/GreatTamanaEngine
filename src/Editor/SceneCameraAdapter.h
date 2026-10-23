#pragma once

// Scene camera control for the HTTP bridge, backed by the Scene panel's last camera overlay frame.

#include "SceneCameraControl.h"

namespace gte {

namespace fx {
struct ViewCullReport;
}

class Registry;
class Renderer;
class RenderTexture;
struct EditorContext;

// The HTTP view of a cull report. Counts that cannot be paired in time are reported invalid.
SceneCameraCullView MakeSceneCameraCullView(const fx::ViewCullReport& report);

class SceneCameraAdapter final : public ISceneCameraControl {
public:
    // Stores non-owning pointers; they stay valid for the whole editor frame, when HTTP commands run.
    void BeginFrame(Renderer& renderer, EditorContext& ctx, Registry& registry, RenderTexture& sceneView) noexcept;

    SceneCameraReport Report() const override;
    SceneCameraResult Select(const std::string& entityName) override;
    SceneCameraPickResult Pick(float x, float y) override;
    std::optional<ClusterViewerImage> CaptureImage(bool overlay) override;

private:
    Renderer* m_renderer = nullptr;
    EditorContext* m_ctx = nullptr;
    Registry* m_registry = nullptr;
    RenderTexture* m_sceneView = nullptr;
};

} // namespace gte
