#pragma once

#include "ECS/Components/Camera.h"
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

    // Pure camera-resolution step, the Camera equivalent of
    // CollectRenderables() above: finds the first entity (in
    // ComponentStorage<Camera> order) with Camera::active == true and
    // combines its Camera::ProjectionMatrix(aspectWidthOverHeight) with
    // Camera::ViewMatrix() of its Transform (an identity Transform - origin,
    // no rotation - if that entity happens not to have one) into a single
    // view-projection matrix. Returns Mat4::Identity() if the Registry has
    // no active Camera at all, which is exactly what preserves this
    // engine's original "vertices are already authored directly in clip
    // space, no camera involved" triangle-demo behavior for a scene that
    // hasn't added a Camera yet. Touches nothing but `registry` - no
    // Renderer, no live GPU resources - so this alone is Tier-1-testable
    // (see tests/Game/RenderSystemTests.cpp) exactly like
    // CollectRenderables() above.
    static Mat4 ResolveActiveCameraViewProjection(Registry& registry, float aspectWidthOverHeight);

    // Resolves each DrawCommand's handles against this RenderSystem's own
    // Mesh/Pipeline pools and submits it to `renderer` - the one step that
    // actually needs a live Renderer, called once per frame PER VISIBLE
    // render target from Game::Render() (a Game view and a Scene view, each
    // with their own RenderTexture/aspect ratio, both showing the identical
    // scene through whatever the active Camera currently is - see
    // Application::Run()). `aspectWidthOverHeight` is the aspect ratio of
    // whichever render target this call's draws will land in - see
    // ResolveActiveCameraViewProjection() above. A DrawCommand whose handle
    // no longer resolves (e.g. a future unloaded mesh) is silently skipped
    // rather than asserting - draws are inherently best-effort against
    // whatever is currently loaded.
    void Draw(Registry& registry, Renderer& renderer, float aspectWidthOverHeight);

    // Explicit-view-projection overload of Draw() above, for a caller that
    // already has its own view-projection matrix to render with instead of
    // resolving one from the ECS Camera component - namely
    // ImGuiEditorLayer's Scene view, which renders through its own
    // independently-orbitable EditorCamera (see
    // src/Editor/EditorCamera.h) rather than whatever ECS entity has the
    // active Camera component (that's still what the float-aspect overload
    // above, used by the Game view, resolves via
    // ResolveActiveCameraViewProjection()). The float-aspect overload above
    // is implemented purely in terms of this one.
    void Draw(Registry& registry, Renderer& renderer, const Mat4& viewProjection);

private:
    ResourcePool<Mesh, MeshHandle> m_meshes;
    ResourcePool<Pipeline, PipelineHandle> m_pipelines;
};

} // namespace gte
