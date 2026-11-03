#pragma once

#include "SceneQuery.h"
#include "../Renderer/RenderGraph/MeshPass.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace gte {

class Registry;
class Renderer;
class RenderSystem;
class SceneServicesDescriptorSet;

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

    // Skips entities whose padded world bounds fall outside the camera's view-projection.
    SceneDrawPass& CullToFrustum(float paddingFraction);

    // Entities the draw skips. Read when the pass executes, so the set must outlive the frame's graph. Null skips none.
    // With `liveEpoch`, a changed epoch at execute time means the set is gone: debug asserts, release skips nothing.
    SceneDrawPass& ExcludeEntities(const EntityMarkSet* entities, const std::uint64_t* liveEpoch = nullptr);

    // Draws only these entities. Same lifetime rules as ExcludeEntities(), but a stale set draws nothing.
    SceneDrawPass& OnlyEntities(const EntityMarkSet* entities, const std::uint64_t* liveEpoch = nullptr);

    // Binds the scene-services set (set = 1) of `view` for every draw. The set is looked up when the pass executes.
    // `publishedSlots` are the textures bound in it; each is declared as a read (`depth` marks depth-format slots).
    SceneDrawPass& SceneServices(const SceneServicesDescriptorSet& services, rg::RenderViewId view,
        std::span<const rg::MeshPassTextureRead> publishedSlots);

    // Declares reads of vertex buffers written by earlier passes (GPU skinning output).
    SceneDrawPass& ReadVertexBuffers(std::span<const rg::BufferHandle> buffers);

    // Declares the pass. False (and one error in the log) when it has no attachment.
    bool Draw();

private:
    rg::MeshPass m_mesh;
    RenderSystem* m_system;
    Registry* m_registry;
    Renderer* m_renderer;
    PipelineOverrideSet m_overrides;
    Mat4 m_viewProjection = Mat4::Identity();
    std::optional<float> m_cullPadding;
    const EntityMarkSet* m_excluded = nullptr;
    const EntityMarkSet* m_only = nullptr;
    const std::uint64_t* m_liveEpoch = nullptr;
    std::uint64_t m_epochAtDeclare = 0;
    const SceneServicesDescriptorSet* m_services = nullptr;
    rg::RenderViewId m_servicesView;
};

} // namespace gte
