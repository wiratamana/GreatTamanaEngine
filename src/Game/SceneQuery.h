#pragma once

#include "ECS/Entity.h"
#include "Math/Mat4.h"
#include "Renderer/PipelineHandle.h"

#include <volk.h>

#include <cstddef>
#include <optional>
#include <unordered_set>

namespace gte {

class Registry;
class Renderer;
class RenderSystem;
class IFrameDebuggerCaptureRecorder;

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

    // When set, OVERRIDES every entity's own MeshRenderer::pipeline - every
    // single renderable draws through THIS ONE Pipeline instead. This is
    // what a shadow-map/depth-prepass/GI-voxelize pass needs: same
    // geometry, same transforms, completely different shader, with ZERO
    // per-entity special-casing.
    //
    // CALLER RESPONSIBILITY, not automatically checked: the Pipeline you
    // pass here must be built with a VertexLayout (see Renderer/Pipeline.h)
    // that every targeted entity's own Mesh vertex buffer actually matches.
    // A Mesh carries no record of its own vertex layout, so there is no way
    // for RenderSystem::Draw() to detect a mismatch - submitting a Mesh
    // built for one layout against a Pipeline expecting a different one is
    // undefined behavior at the Vulkan level (wrong stride/attribute
    // reads), not a safely-skipped draw. Do not point this at the whole
    // scene until you have confirmed every entity you intend to reach
    // shares a vertex layout your override Pipeline actually expects -
    // start with a known, curated subset rather than "the entire scene" if
    // the scene mixes primitive shapes, imported meshes, textured
    // submeshes, and GPU-driven batched meshes (it very likely does).
    //
    // If this handle is stale/invalid/never-registered, RenderSystem::
    // Draw() logs exactly ONE warning (see RenderSystem.h/.cpp) and then
    // silently skips every entity for this call - it does not assert, and
    // it does not fall back to each entity's own original pipeline.
    std::optional<PipelineHandle> pipelineOverride = std::nullopt;

    // Non-owning. Must stay valid for the duration of this one DrawScene()
    // call only - see this struct's own LIFETIME CONTRACT comment above.
    IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr;
    std::optional<std::size_t> maxDrawCount = std::nullopt;

    // Non-owning, nullptr == empty set. Must stay valid for the duration of
    // this one DrawScene() call only - see this struct's own LIFETIME
    // CONTRACT comment above.
    const std::unordered_set<Entity>* batchedEntities = nullptr;

    // Block 4 (task_manager/better-render-pass-6) - the ONE resolved,
    // per-view, per-frame "scene services" descriptor set (set = 1) to bind
    // for every entity drawn by this one DrawScene() call - VK_NULL_HANDLE
    // means "no scene services this call" (every existing/other caller stays
    // unaffected). NEVER resolved per-entity - this is a single, call-scoped
    // constant, mirroring pipelineOverride's own "one value for the whole
    // call" contract above.
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
