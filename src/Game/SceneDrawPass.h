#pragma once

#include "SceneQuery.h"
#include "../Renderer/RenderGraph/MeshPass.h"

#include <array>
#include <optional>

namespace gte {

class Registry;
class Renderer;
class RenderSystem;

// The request a scene redraw hands to DrawScene: the camera and the per-layout pipeline overrides.
SceneDrawRequest MakeSceneDrawRequest(const Mat4& viewProjection, const PipelineOverrideSet& overrides);

// Redraws the scene into the given attachments. The draw callback captures by value; the engine objects
// must outlive the frame. Usage: SceneDrawPass(...).Pipelines(set).Camera(vp).Depth(map, 1.0f).Draw();
class SceneDrawPass {
public:
    SceneDrawPass(rg::RenderGraphBuilder& builder, RenderSystem& system, Registry& registry, Renderer& renderer,
        rg::StaticName name, rg::ViewScope scope, rg::RenderPassEvent event);

    SceneDrawPass(const SceneDrawPass&) = delete;
    SceneDrawPass& operator=(const SceneDrawPass&) = delete;

    SceneDrawPass& Pipelines(const PipelineOverrideSet& overrides);
    SceneDrawPass& Camera(const Mat4& viewProjection);
    SceneDrawPass& Color(rg::TextureHandle target, std::optional<std::array<float, 4>> clear = std::nullopt);
    SceneDrawPass& Depth(rg::TextureHandle target, std::optional<float> clear = std::nullopt);

    // Declares the pass. False (and one error in the log) when it has no attachment.
    bool Draw();

private:
    rg::MeshPass m_mesh;
    RenderSystem* m_system;
    Registry* m_registry;
    Renderer* m_renderer;
    PipelineOverrideSet m_overrides;
    Mat4 m_viewProjection = Mat4::Identity();
};

} // namespace gte
