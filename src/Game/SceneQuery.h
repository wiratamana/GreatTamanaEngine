#pragma once

#include "ECS/Entity.h"
#include "Math/Mat4.h"
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"

#include <volk.h>

#include <array>
#include <cstddef>
#include <optional>
#include <unordered_set>

namespace gte {

class Registry;
class Renderer;
class RenderSystem;
class IFrameDebuggerCaptureRecorder;

// One optional override Pipeline per VertexLayout - lets a single DrawScene()
// call route primitives, untextured meshes, and textured meshes each through
// their own matching override pipeline (e.g. a depth-only pass, an outline
// pass). An entity whose own pipeline's layout has no entry here is skipped
// entirely for that call - it never falls back to its own original pipeline.
struct PipelineOverrideSet {
    std::array<std::optional<PipelineHandle>, kVertexLayoutCount> byLayout;
};

// Pure decision: which override pipeline (if any) matches entityLayout. No
// Renderer/Pipeline pool involved - safe to unit test directly.
inline std::optional<PipelineHandle> ResolveOverridePipeline(
    const PipelineOverrideSet& overrides, VertexLayout entityLayout)
{
    return overrides.byLayout[static_cast<std::size_t>(entityLayout)];
}

// Deliberately a PLAIN, pure-data struct - no behavior, no methods, mirrors
// this codebase's existing "describe intent as data, not as a deep call
// chain" convention (see RenderPassDesc, TextureDesc, etc.).
//
// LIFETIME CONTRACT (read before storing one of these anywhere): every
// pointer field below (`frameDebuggerCapture`, `batchedEntities`) is a
// plain, non-owning observer, meant to be read exactly once, SYNCHRONOUSLY,
// for the duration of the single DrawScene() call it is passed into - the
// same lifetime rule RenderSystem::Draw()'s own `capture`/`batchedEntities`
// parameters already carry today, just now reachable through a struct field
// instead of a direct function argument. Do NOT build one of these once and
// reuse it across multiple frames/calls while letting `batchedEntities` keep
// pointing at a set that may not outlive this frame - construct (or refresh
// every field of) a fresh SceneDrawRequest for each DrawScene() call
// instead.
struct SceneDrawRequest {
    Mat4 viewProjection = Mat4::Identity();

    // Per-vertex-layout override table. When set, resolves per entity via
    // ResolveOverridePipeline() - lets one call draw primitives, untextured
    // meshes, and textured meshes each through their own matching pipeline
    // in a single pass. An entity whose layout has no entry is skipped,
    // never drawn with its own original pipeline. nullopt (default)
    // disables overriding entirely: every entity draws through its own
    // MeshRenderer::pipeline, unchanged.
    std::optional<PipelineOverrideSet> pipelineOverrideSet = std::nullopt;

    // Non-owning. Must stay valid for the duration of this one DrawScene()
    // call only - see this struct's own LIFETIME CONTRACT comment above.
    IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr;
    std::optional<std::size_t> maxDrawCount = std::nullopt;

    // Non-owning, nullptr == empty set. Must stay valid for the duration of
    // this one DrawScene() call only - see this struct's own LIFETIME
    // CONTRACT comment above.
    const std::unordered_set<Entity>* batchedEntities = nullptr;

    // The ONE resolved, per-view, per-frame "scene services" descriptor set
    // (set = 1) to bind for every entity drawn by this one DrawScene() call -
    // VK_NULL_HANDLE means "no scene services this call". NEVER resolved
    // per-entity - this is a single, call-scoped constant, mirroring
    // pipelineOverrideSet's own "one value for the whole call" contract
    // above.
    VkDescriptorSet sceneServicesSet = VK_NULL_HANDLE;
};

// The ONE new public convenience entry point every "redraw the scene for a
// different purpose" pass can call - built-in passes AND external Project
// Assembly passes both go through this, so there is only ever ONE
// scene-drawing code path in the whole engine, not "the real one
// RenderSystem.cpp uses" plus "a second one plugins are stuck
// re-implementing". Thin, one-call forward into RenderSystem::Draw().
//
// `renderSystem` is reachable from a Project Assembly's own registered
// callback via `core.GetGame().GetRenderSystem()`; the matching
// `registry`/`renderer` come from that SAME `core` via
// `core.GetRegistry()`/`core.GetRenderer()`.
void DrawScene(RenderSystem& renderSystem, Registry& registry, Renderer& renderer,
    const SceneDrawRequest& request);

} // namespace gte
