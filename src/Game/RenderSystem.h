#pragma once

#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Math/Mat4.h"
#include "Renderer/Mesh.h"
#include "Renderer/MeshHandle.h"
#include "Renderer/Pipeline.h"
#include "Renderer/PipelineHandle.h"
#include "Renderer/ResourcePool.h"

#include <vector>

namespace gte {

class Renderer;

// One queued draw call's worth of PLAIN data, extracted from the ECS world -
// a MeshHandle/PipelineHandle pair (never a Mesh&/Pipeline* - see
// ECS/Components/MeshRenderer.h) plus the world matrix to draw it with.
// Carries no live Renderer/Vulkan state at all, which is what keeps
// RenderSystem::CollectRenderables() callable with nothing but a Registry -
// no live GPU device, no Renderer, no ResourcePool needed - see AGENTS.md
// ("Testability & Regression Safety").
struct DrawCommand {
    MeshHandle mesh;
    PipelineHandle pipeline;
    Mat4 model = Mat4::Identity(); // Mat4's own default ctor is all-zero, NOT identity - see Math/Mat4.h.
};

// The "middleman" between the ECS world and Renderer (see AGENTS.md, Clean
// Architecture): the ONLY thing in the engine allowed to depend on both
// Registry/Transform/MeshRenderer AND Renderer/Mesh/Pipeline. The dependency
// only ever points this one direction - Renderer itself never depends on
// ECS in any way (Renderer::Submit() takes a plain Mat4, never an
// Entity/Registry), matching the same "only Application knows about SDL"
// boundary rule this engine already applies elsewhere.
//
// Owns the actual Mesh/Pipeline objects Game creates via
// Renderer::CreateMesh()/CreatePipeline() - still returned BY VALUE exactly
// as before (Renderer's own factory API is completely unchanged) - addressed
// by the MeshHandle/PipelineHandle a MeshRenderer component can safely hold
// instead of ever embedding a Mesh/Pipeline directly.
class RenderSystem {
public:
    RenderSystem() = default;

    // Takes ownership of a Mesh/Pipeline Game already created via
    // Renderer::CreateMesh()/CreatePipeline(), returning the handle a
    // MeshRenderer component should store.
    MeshHandle RegisterMesh(Mesh&& mesh) { return m_meshes.Insert(std::move(mesh)); }
    PipelineHandle RegisterPipeline(Pipeline&& pipeline) { return m_pipelines.Insert(std::move(pipeline)); }

    // Pure data-collection step: every entity with a MeshRenderer becomes
    // one DrawCommand, using its Transform's LocalToWorldMatrix() if present
    // (Mat4::Identity() otherwise - an entity can have a MeshRenderer with
    // no Transform and just draws at the origin). Touches nothing but
    // `registry` - no Renderer, no live GPU resources - so this alone is
    // Tier-1-testable (see tests/Game/RenderSystemTests.cpp) even though
    // RenderSystem as a whole is not (it owns real Mesh/Pipeline objects).
    static std::vector<DrawCommand> CollectRenderables(Registry& registry);

    // Resolves each DrawCommand's handles against this RenderSystem's own
    // Mesh/Pipeline pools and submits it to `renderer` - the one step that
    // actually needs a live Renderer, called once per frame from
    // Game::Render(). A DrawCommand whose handle no longer resolves (e.g. a
    // future unloaded mesh) is silently skipped rather than asserting -
    // draws are inherently best-effort against whatever is currently
    // loaded.
    void Draw(Registry& registry, Renderer& renderer);

private:
    ResourcePool<Mesh, MeshHandle> m_meshes;
    ResourcePool<Pipeline, PipelineHandle> m_pipelines;
};

} // namespace gte
