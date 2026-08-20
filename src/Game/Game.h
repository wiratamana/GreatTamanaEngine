#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "ECS/Registry.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "RenderSystem.h"

#include <array>

namespace gte {

class Renderer;

// Sits on top of Window/Renderer and has no direct knowledge of SDL, or of
// Vulkan beyond the Renderer abstraction. Owns the ECS World (m_registry)
// plus RenderSystem (src/Game/RenderSystem.h - the one thing allowed to
// depend on both ECS and Renderer) instead of holding a hardcoded
// Pipeline/Mesh pair directly: Game never touches a Mesh/Pipeline/Vulkan
// handle itself anymore, it only ever creates entities/components and calls
// RenderSystem::Draw(). All engine/game logic lives here (and in whatever
// this grows into).
class Game {
public:
    Game() = default;

    // Called once per discrete/translated event, before Update() runs for
    // that frame. Use this for one-shot reactions (a key just pressed,
    // window resized, etc) - anything continuous ("is this key held") should
    // be read from the InputState passed to Update() instead.
    void OnEvent(const Event& event);

    void Update(double deltaSeconds, const InputState& input);

    // Sets the clear color and, via RenderSystem::Draw(), queues this
    // frame's draw calls for every entity that has a MeshRenderer.
    // `aspectWidthOverHeight` is the aspect ratio of whichever render
    // target this call's draws will land in (a Game view and a Scene view,
    // each with their own RenderTexture/aspect, can each call this once per
    // frame - see RenderSystem::Draw()). By default (viewProjectionOverride
    // == nullptr) the view-projection matrix is resolved from whichever ECS
    // entity has the active Camera component (see ECS/Components/Camera.h)
    // - what the Editor's "Game" view uses. Passing a non-null
    // viewProjectionOverride instead renders with THAT matrix verbatim,
    // bypassing ECS camera resolution entirely - what the Editor's "Scene"
    // view uses instead, to render through its own independently-
    // orbitable EditorCamera (src/Editor/EditorCamera.h) rather than
    // whatever ECS entity happens to be the active gameplay Camera. Game
    // itself has no idea the Editor/EditorCamera exist either way - this is
    // just a plain Mat4*, decided by Application (the composition root),
    // same spirit as everything else in this comment. Does NOT call
    // Renderer::Present()/RenderOffscreen() itself - *where* this frame
    // ends up (the swapchain, fullscreen, or one of the Editor's off-screen
    // "Game view"/"Scene view" RenderTextures) is decided by Application
    // too. See Application::Run().
    void Render(Renderer& renderer, float aspectWidthOverHeight, const Mat4* viewProjectionOverride = nullptr);

    // Read-only-in-spirit access to the ECS World for the Editor's
    // Hierarchy/Inspector panels (src/Editor/ImGuiEditorLayer.cpp) to
    // observe/edit - the exact "Editor only ever *observes* Game ... through
    // its existing public accessors" boundary the class comment above (and
    // EditorLayer.h) already documents. Non-const because the Inspector
    // panel edits component fields (e.g. dragging a Transform's position) in
    // place; Game itself never calls this on its own registry.
    Registry& GetRegistry() noexcept { return m_registry; }

    // Spawns a new entity built from one of the engine's built-in primitive
    // shapes (PrimitiveType - see Renderer/Primitives/
    // PrimitiveMeshGenerator.h): a Transform at the origin (identity
    // rotation/scale) plus a MeshRenderer referencing that shape's mesh -
    // this engine's equivalent of Unity's GameObject.CreatePrimitive(), and
    // deliberately a RUNTIME/Game-level operation rather than an Editor-only
    // one (a real game could call this to spawn primitives too, exactly like
    // Unity's own API is not editor-only). The Editor's "Hierarchy" right-
    // click "Create 3D Object" menu (src/Editor/Panels/HierarchyPanel.cpp)
    // is just the first caller of it. Needs `renderer` to build/upload the
    // shape's GPU mesh the first time that particular PrimitiveType is ever
    // requested (see EnsurePrimitiveMesh() below); every subsequent call for
    // the SAME PrimitiveType reuses the already-uploaded mesh, exactly like
    // the demo scene's three triangle entities already share one Mesh/
    // Pipeline pair (see EnsureDemoSceneBuilt()) - only Transform differs
    // per instance.
    Entity CreatePrimitiveEntity(Renderer& renderer, PrimitiveType type);

private:
    // Lazily builds the demo scene - three entities sharing one triangle
    // Mesh/Pipeline, spaced left/center/right purely via Transform, plus one
    // Camera entity positioned back along -Z looking at them - on the first
    // Render() call (needs a Renderer to build GPU resources through).
    // Proves the ECS -> RenderSystem -> Renderer pipeline end to end:
    // multiple entities, one shared mesh/pipeline, independently positioned
    // via push-constant model matrices, all viewed through a real
    // view-projection matrix rather than vertices authored directly in clip
    // space. Will be replaced by a real scene/asset-loading system once
    // there's more than a hardcoded demo scene.
    void EnsureDemoSceneBuilt(Renderer& renderer);

    // Lazily creates (once) the one shared unlit Pipeline every entity this
    // engine draws currently uses - the demo triangles AND every primitive
    // entity from CreatePrimitiveEntity() alike - so a primitive can be
    // spawned before EnsureDemoSceneBuilt() ever runs (e.g. the very first
    // frame's Hierarchy right-click) without depending on the demo scene's
    // own lazy-init order. Extracted out of EnsureDemoSceneBuilt() rather
    // than duplicated, so there is exactly one Pipeline (and one
    // PipelineHandle) for the whole process's lifetime, same "share, don't
    // duplicate" spirit as EnsurePrimitiveMesh() below.
    PipelineHandle EnsureDefaultPipeline(Renderer& renderer);

    // Lazily creates (once per distinct PrimitiveType) and thereafter
    // reuses that shape's GPU mesh - every "Create Cube" click shares the
    // exact same MeshHandle, only each entity's own Transform differs,
    // exactly mirroring Unity's own built-in primitives (every Cube you
    // create shares one built-in mesh asset) and this engine's existing
    // demo-scene triangles (see EnsureDemoSceneBuilt()).
    MeshHandle EnsurePrimitiveMesh(Renderer& renderer, PrimitiveType type);

    Registry m_registry;
    RenderSystem m_renderSystem;
    bool m_demoSceneBuilt = false;

    PipelineHandle m_defaultPipeline;

    // Indexed by static_cast<std::size_t>(PrimitiveType) - kInvalidMeshHandle
    // (the array's default-constructed value) means "not generated yet".
    std::array<MeshHandle, 5> m_primitiveMeshes;
};

} // namespace gte
