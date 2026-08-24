#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "ECS/Registry.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "Renderer/TextureHandle.h"
#include "RenderSystem.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

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

    // Spawns a whole hierarchy of entities from an imported *.gta
    // AssetType::Mesh file (see src/Assets/MeshFile.h/PmxLoader.h - the
    // result of importing a MikuMikuDance .pmx model via the Editor's
    // "Project" panel) at `absoluteGtaPath` - this engine's answer to "drag
    // a model asset into Hierarchy/Scene to instantiate it", the Mesh-asset
    // equivalent of CreatePrimitiveEntity() above.
    //
    // A multi-part model (see below for what "part" means) is spawned as a
    // real parent/child hierarchy (ECS/TransformHierarchy.h/Transform.h),
    // not a flat list of independent siblings: ONE plain, empty ROOT entity
    // (an identity Transform only - no MeshRenderer of its own, so it never
    // renders anything by itself) is created first, named after the asset
    // FILE itself (`absoluteGtaPath`'s own filename, minus its ".gta"
    // extension - e.g. "Miku.gta" spawns a root named "Miku"), and every
    // part entity below is attached under it via SetParent() - moving/
    // rotating/scaling the root moves the WHOLE model together, exactly
    // like dragging a multi-material FBX/glTF model into a Unity scene
    // creates one root GameObject with a child per submesh. Every part
    // entity keeps an identity LOCAL Transform (so it renders exactly where
    // it always did - the root itself starts at the world origin too); the
    // caller (a future Inspector edit, or a real scene file) is responsible
    // for moving the ROOT anywhere else afterwards - moving it alone is
    // enough to move every part with it.
    //
    // MORE THAN ONE part entity is spawned when the source .pmx defines
    // materials with a resolvable diffuse texture (see MaterialData.h): one
    // part PER DISTINCT "untextured" combined submesh (there is at most one
    // of these - every material with no resolvable texture is merged into a
    // single combined Mesh, rendered exactly like a pre-material-import
    // model always was, via the plain "grey clay" Mesh.vert/.frag pipeline)
    // PLUS one part per material that DOES have a resolvable texture, each
    // with its own MeshRenderer::texture bound and rendered through the
    // textured TexturedMesh.vert/.frag pipeline (see Pipeline.h's
    // VertexLayout::PositionNormalUv). A materialless mesh (or one whose
    // *.gta predates this engine's material-import support) still spawns
    // exactly the one untextured part it always did (as the root's only
    // child). Each TEXTURED part is named after the PMX material it came
    // from (Material::name - see MaterialData.h) whenever that material
    // actually has a non-empty name; the combined untextured part (which
    // may merge more than one material into one submesh) and any part whose
    // originating material has no name are left with no Name component at
    // all - "Hierarchy" then falls back to its usual synthesized "Entity
    // %u" label for that entity (see Panels/HierarchyPanel.cpp).
    //
    // The mesh's positions/normals/UVs/triangle indices (split per-material
    // this way) plus every distinct diffuse texture are uploaded ONCE per
    // distinct `absoluteGtaPath`/texture path and cached (see
    // m_meshAssetCache/m_materialTextureCache below) - a second call for the
    // SAME asset (e.g. dragging it into Hierarchy twice) reuses the
    // already-uploaded GPU resources, exactly like EnsurePrimitiveMesh()
    // already does per PrimitiveType; only new ENTITIES (and their
    // Transforms) are created each time. This model has no bone/morph
    // deformation applied yet either (see TODO.md, "Real MMD skinning/
    // animation runtime" - the skeleton/skin-weight DATA already round-trips
    // through the same *.gta's metadata section via RigFile.h, but nothing
    // evaluates it at runtime yet, so this always renders the model's
    // original bind pose exactly as authored).
    //
    // Returns kInvalidEntity (never throws, and creates NO entities at all)
    // if `absoluteGtaPath` doesn't currently resolve to a valid, non-empty
    // *.gta AssetType::Mesh file (missing file, bad magic/wrong asset type,
    // corrupt/truncated payload, or zero vertices/triangles) - the caller
    // (HierarchyPanel/ScenePanel's drag-and-drop target) should simply
    // ignore the drop in that case, same "degrade gracefully" convention as
    // every other Editor drag-and-drop path (see AGENTS.md, "Editor Module
    // Structure"). Otherwise returns the newly created ROOT entity - a
    // caller that only keeps one handle (e.g. to select it in the
    // Hierarchy, or to move/parent the whole model elsewhere) gets exactly
    // the one that represents the whole instantiated model, Unity's own
    // "the thing you dragged in is the root you select" convention; every
    // part is still a live child entity in the Registry either way.
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
    // UNTEXTURED imported-mesh submesh uses (Shaders/Mesh.vert/.frag,
    // VertexLayout::PositionNormal) - a completely separate Pipeline/
    // PipelineHandle from EnsureDefaultPipeline() above, since it's built
    // against a different vertex layout (see Pipeline.h's VertexLayout)
    // and cannot legally draw a position+color Mesh (or vice versa).
    PipelineHandle EnsureMeshPipeline(Renderer& renderer);

    // Lazily creates (once) the one shared TEXTURED Pipeline every textured
    // imported-mesh submesh uses (Shaders/TexturedMesh.vert/.frag,
    // VertexLayout::PositionNormalUv, useMaterialTexture = true) - see
    // EnsureMeshPipeline() above for why this is a separate Pipeline/
    // PipelineHandle from it.
    PipelineHandle EnsureTexturedMeshPipeline(Renderer& renderer);

    // Lazily decodes/uploads (once per distinct absolute texture path, then
    // cached in m_materialTextureCache) a PMX material's diffuse texture
    // file straight off disk (see ImageFileDecoder.h) into a MaterialTexture
    // (Renderer/MaterialTexture.h) - shared across every submesh/model that
    // happens to reference the exact same texture file. Returns
    // kInvalidTextureHandle (never throws) if `absoluteTexturePath` is
    // empty or fails to decode (missing/corrupt file - see
    // MaterialData::textures' own doc comment for why this is expected to
    // legitimately happen sometimes).
    TextureHandle EnsureMaterialTexture(Renderer& renderer, const std::string& absoluteTexturePath);

    // One per entity CreateMeshEntityFromGtaFile() should spawn (as a CHILD
    // of that call's own root entity) for a given *.gta asset - see
    // EnsureMeshAsset() below.
    struct MeshAssetPart {
        MeshHandle mesh;
        // kInvalidTextureHandle means "untextured - draw via
        // EnsureMeshPipeline()'s pipeline"; otherwise draw via
        // EnsureTexturedMeshPipeline()'s pipeline with this texture bound.
        TextureHandle texture;
        // The originating PMX material's own name (Material::name - see
        // MaterialData.h), or empty when this part has no single
        // originating material with a usable name (the combined untextured
        // submesh, which may merge more than one material together, or a
        // material whose own name PMX left blank) - see
        // CreateMeshEntityFromGtaFile()'s own doc comment (Game.h) for what
        // an empty name here means for the spawned entity.
        std::string name;
    };

    // Decodes (once per distinct `absoluteGtaPath`, then cached in
    // m_meshAssetCache) a *.gta AssetType::Mesh file's MeshData payload +
    // RigFileData metadata (for its MaterialData - see RigFile.h) into one
    // GPU MeshAssetPart per entity CreateMeshEntityFromGtaFile() should
    // spawn - the actual work behind that function. Returns an empty
    // vector (never throws) for anything that doesn't resolve to a valid,
    // non-empty Mesh *.gta - see CreateMeshEntityFromGtaFile()'s own doc
    // comment for the exact failure cases.
    const std::vector<MeshAssetPart>& EnsureMeshAsset(Renderer& renderer, const std::string& absoluteGtaPath);

    Registry m_registry;
    RenderSystem m_renderSystem;
    bool m_demoSceneBuilt = false;

    PipelineHandle m_defaultPipeline;

    // Indexed by static_cast<std::size_t>(PrimitiveType) - kInvalidMeshHandle
    // (the array's default-constructed value) means "not generated yet".
    std::array<MeshHandle, 5> m_primitiveMeshes;

    PipelineHandle m_meshPipeline;
    PipelineHandle m_texturedMeshPipeline;

    // Keyed by absolute *.gta filesystem path (UTF-8) - see
    // EnsureMeshAsset()/CreateMeshEntityFromGtaFile() above. A plain
    // std::string key (rather than a Guid) since the caller (HierarchyPanel/
    // ScenePanel's drag-and-drop target) only ever has a filesystem path
    // in hand, not an AssetDatabase lookup - Game itself never depends on
    // AssetDatabase/Editor code (see AGENTS.md, Clean Architecture).
    std::unordered_map<std::string, std::vector<MeshAssetPart>> m_meshAssetCache;

    // Keyed by absolute texture filesystem path (UTF-8) - see
    // EnsureMaterialTexture() above. Shared across every model/material
    // that references the same texture file (e.g. a body texture reused by
    // several materials in one .pmx).
    std::unordered_map<std::string, TextureHandle> m_materialTextureCache;
};

} // namespace gte
