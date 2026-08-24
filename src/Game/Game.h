#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "ECS/Registry.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "RenderSystem.h"
#include <array>
#include <string>
#include <unordered_map>

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

    // Spawns a new entity from an imported *.gta AssetType::Mesh file (see
    // src/Assets/MeshFile.h/PmxLoader.h - the result of importing a
    // MikuMikuDance .pmx model via the Editor's "Project" panel) at
    // `absoluteGtaPath` - this engine's answer to "drag a model asset into
    // Hierarchy/Scene to instantiate it", the Mesh-asset equivalent of
    // CreatePrimitiveEntity() above. Spawns at the world origin with an
    // identity Transform (scale/rotation), exactly like
    // CreatePrimitiveEntity() - the caller (a future Inspector edit, or a
    // real scene file) is responsible for moving it anywhere else.
    //
    // The mesh's positions/normals/triangle indices are uploaded ONCE per
    // distinct `absoluteGtaPath` and cached (see m_meshAssetCache below) - a
    // second entity spawned from the SAME asset (e.g. dragging it into
    // Hierarchy twice) reuses the already-uploaded GPU mesh, exactly like
    // EnsurePrimitiveMesh() already does per PrimitiveType. Rendered through
    // a plain, unlit-but-normal-shaded "grey clay" pipeline (Shaders/
    // Mesh.vert/.frag, position+normal only - see EnsureMeshPipeline()/
    // Renderer::CreatePipeline()'s VertexLayout::PositionNormal) since a
    // *.gta Mesh payload carries no material/texture data yet (see TODO.md,
    // "PMX material/texture import") and this model has no bone/morph
    // deformation applied yet either (see TODO.md, "Real MMD skinning/
    // animation runtime" - the skeleton/skin-weight DATA already
    // round-trips through the same *.gta's metadata section via
    // RigFile.h, but nothing evaluates it at runtime yet, so this always
    // renders the model's original bind pose exactly as authored).
    //
    // Returns kInvalidEntity (never throws) if `absoluteGtaPath` doesn't
    // currently resolve to a valid, non-empty *.gta AssetType::Mesh file
    // (missing file, bad magic/wrong asset type, corrupt/truncated payload,
    // or zero vertices/triangles) - the caller (HierarchyPanel/ScenePanel's
    // drag-and-drop target) should simply ignore the drop in that case,
    // same "degrade gracefully" convention as every other Editor
    // drag-and-drop path (see AGENTS.md, "Editor Module Structure").
    Entity CreateMeshEntityFromGtaFile(Renderer& renderer, const std::string& absoluteGtaPath);

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

    // Lazily creates (once) the one shared "grey clay" Pipeline every
    // imported mesh entity from CreateMeshEntityFromGtaFile() uses (Shaders/
    // Mesh.vert/.frag, VertexLayout::PositionNormal) - a completely separate
    // Pipeline/PipelineHandle from EnsureDefaultPipeline() above, since it's
    // built against a different vertex layout (see Pipeline.h's
    // VertexLayout) and cannot legally draw a position+color Mesh (or vice
    // versa).
    PipelineHandle EnsureMeshPipeline(Renderer& renderer);

    // Decodes/uploads (once per distinct `absoluteGtaPath`, then cached) the
    // MeshData payload of a *.gta AssetType::Mesh file into a real, indexed
    // GPU Mesh (MeshVertex/Renderer::CreateMesh()'s indexed overload) - the
    // actual work behind CreateMeshEntityFromGtaFile() above. Returns
    // kInvalidMeshHandle (never throws) for anything that doesn't resolve to
    // a valid, non-empty Mesh *.gta - see CreateMeshEntityFromGtaFile()'s
    // own doc comment for the exact failure cases.
    MeshHandle EnsureMeshAsset(Renderer& renderer, const std::string& absoluteGtaPath);

    Registry m_registry;
    RenderSystem m_renderSystem;
    bool m_demoSceneBuilt = false;

    PipelineHandle m_defaultPipeline;

    // Indexed by static_cast<std::size_t>(PrimitiveType) - kInvalidMeshHandle
    // (the array's default-constructed value) means "not generated yet".
    std::array<MeshHandle, 5> m_primitiveMeshes;

    PipelineHandle m_meshPipeline;

    // Keyed by absolute *.gta filesystem path (UTF-8) - see
    // EnsureMeshAsset()/CreateMeshEntityFromGtaFile() above. A plain
    // std::string key (rather than a Guid) since the caller (HierarchyPanel/
    // ScenePanel's drag-and-drop target) only ever has a filesystem path
    // in hand, not an AssetDatabase lookup - Game itself never depends on
    // AssetDatabase/Editor code (see AGENTS.md, Clean Architecture).
    std::unordered_map<std::string, MeshHandle> m_meshAssetCache;
};

} // namespace gte
