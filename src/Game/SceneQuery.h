#pragma once

#include "ECS/Entity.h"
#include "Math/Mat4.h"
#include "../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"

#include <array>
#include <cstddef>
#include <optional>
#include <unordered_set>

namespace gte {

class Registry;
class Renderer;
class RenderSystem;
class IFrameDebuggerCaptureRecorder;

// One optional override Pipeline per VertexLayout. An entity whose layout has no entry is skipped,
// never drawn with its own pipeline.
struct PipelineOverrideSet {
    std::array<std::optional<PipelineHandle>, kVertexLayoutCount> byLayout;
};

// Override pipeline for `entityLayout`, if any. Pure.
inline std::optional<PipelineHandle> ResolveOverridePipeline(
    const PipelineOverrideSet& overrides, VertexLayout entityLayout)
{
    return overrides.byLayout[static_cast<std::size_t>(entityLayout)];
}

// Skip entities whose world bounds (padded by a fraction of their largest extent) lie outside this view-projection.
struct SceneCullRequest {
    Mat4 viewProjection = Mat4::Identity();
    float paddingFraction = 0.0f;
};

// Plain data describing one DrawScene() call.
// The pointer fields are non-owning and read only during that call; build a fresh request per call.
struct SceneDrawRequest {
    Mat4 viewProjection = Mat4::Identity();

    // Per-layout override pipelines; nullopt draws every entity with its own MeshRenderer::pipeline.
    std::optional<PipelineOverrideSet> pipelineOverrideSet = std::nullopt;

    IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr;
    std::optional<std::size_t> maxDrawCount = std::nullopt;

    // Entities drawn by GPU-driven batches instead; nullptr is an empty set.
    const std::unordered_set<Entity>* batchedEntities = nullptr;

    // Scene-services descriptor set (set = 1) bound for every draw of this call; Invalid binds none.
    DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid;

    // Frustum cull applied per entity; nullopt draws everything.
    std::optional<SceneCullRequest> cull = std::nullopt;
};

// The single scene-drawing entry point for built-in and project passes; forwards to RenderSystem::Draw().
void DrawScene(RenderSystem& renderSystem, Registry& registry, Renderer& renderer,
    const SceneDrawRequest& request);

} // namespace gte
