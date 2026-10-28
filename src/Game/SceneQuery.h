#pragma once

#include "ECS/Entity.h"
#include "Math/Mat4.h"
#include "../Core/GraphicsHandles.h" // DescriptorSetHandle
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

// Per-call cull tallies over entities the draw looked at (batched and past-maxDrawCount ones are not counted).
struct SceneCullCounts {
    std::uint32_t tested = 0;    // Had local bounds and were tested.
    std::uint32_t culled = 0;    // Tested and outside.
    std::uint32_t unbounded = 0; // No local bounds; always kept.
};

// Skip entities whose world bounds (padded by a fraction of their largest extent) lie outside this view-projection.
// `counts` is non-owning, zeroed and written only during the draw call; nullptr skips tallying.
struct SceneCullRequest {
    Mat4 viewProjection = Mat4::Identity();
    float paddingFraction = 0.0f;
    SceneCullCounts* counts = nullptr;
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

// The request's excluded set by reference (the caller's own set, or one shared empty set). Never a copy.
const std::unordered_set<Entity>& ExcludedEntitiesOf(const SceneDrawRequest& request) noexcept;

// The single scene-drawing entry point for built-in and project passes; forwards to RenderSystem::Draw().
void DrawScene(RenderSystem& renderSystem, Registry& registry, Renderer& renderer,
    const SceneDrawRequest& request);

} // namespace gte
