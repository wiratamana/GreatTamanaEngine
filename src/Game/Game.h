#pragma once

#include "../Animation/MotionSampler.h"
#include "../Assets/AssetTypes.h"
#include "../Assets/MeshData.h"
#include "../Assets/MotionData.h"
#include "../Assets/SkeletonData.h"
#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "../Math/Vec2.h"
#include "../Math/Vec3.h"
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
class AssetDatabase;

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

    // Assigns/replaces the SkeletalAnimator component on `targetEntity` (a
    // live entity spawned by CreateMeshEntityFromGtaFile() - i.e. one that
    // carries a MeshAssetSource component, see ECS/Components/
    // MeshAssetSource.h) so it plays back `absoluteAnimationGtaPath`'s
    // motion (a *.gta AssetType::Animation - see Assets/VmdLoader.h/
    // MotionFile.h) against its own model's skeleton
    // (Assets/SkeletonData.h), driving every one of that model's mesh
    // parts with real bone-deformed positions/normals from the next
    // Update() call onward - this is what makes a spawned MMD model
    // actually animate instead of always rendering its original bind pose
    // (see TODO.md, "Real MMD skinning/animation runtime").
    //
    // HOW A BONE/WEIGHT MISMATCH BETWEEN THE MODEL AND THE MOTION IS
    // HANDLED: a `.vmd` motion is authored independently of any one
    // model's own bone numbering - it names every bone it drives by a
    // human-authored NAME STRING, not an index (see MotionData.h's own
    // file comment) - so the SAME motion is routinely replayed against a
    // model whose skeleton doesn't exactly match the one it was authored
    // against (different bone COUNT, different NAMES for extra/renamed
    // bones, a different subset of "helper"/twist bones, ...). This engine
    // resolves the two purely by NAME (see Animation/MotionSampler.h's
    // ResolveBoneTracksToSkeleton()), and tolerates a mismatch in EITHER
    // direction rather than failing: a skeleton bone with no matching
    // motion track simply never receives any animated offset and stays
    // exactly at its authored bind pose for the whole clip (see
    // Animation/BoneLocalOffset.h's own doc comment) - it does not go
    // missing, jitter, or snap to the origin; a motion bone track whose
    // name doesn't match ANY bone in the skeleton is simply never applied
    // to anything. No "closest name" fuzzy-matching or index-based
    // fallback is attempted - an exact name match is the only thing MMD
    // authoring tools themselves rely on, so that's the only contract this
    // engine honors too. Per-vertex skin WEIGHTS never need any such
    // reconciliation at all - they already reference bone INDICES within
    // this model's own SkeletonData (produced by the same PmxLoader.cpp
    // import, always internally consistent - see MeshData::skinWeights)
    // and are never touched by which motion (if any) ends up playing.
    //
    // Returns false (no component added/changed) if `targetEntity` isn't a
    // live entity with a MeshAssetSource component, its own model has no
    // skeleton/skin-weight data at all (a boneless mesh, or one imported
    // before rig extraction existed), or `absoluteAnimationGtaPath` doesn't
    // currently resolve to a valid, non-empty *.gta AssetType::Animation
    // file - same "degrade gracefully, never throw" convention as
    // CreateMeshEntityFromGtaFile(). Calling this again for an entity that
    // is already animating (e.g. to switch clips) simply replaces its
    // SkeletalAnimator state, restarting playback from frame 0.
    //
    // KNOWN LIMITATION (documented, not an oversight - see TODO.md): every
    // entity spawned from the SAME `absoluteGtaPath` shares the exact same
    // underlying GPU mesh buffers (see EnsureMeshAsset()'s own doc
    // comment) - playing DIFFERENT animations (or the same animation at
    // different times) on two simultaneously-alive instances of the same
    // model will visibly fight over those shared buffers, since both write
    // into them every frame. Fine for today's "one animated instance at a
    // time" use case; per-instance GPU mesh buffers for a rigged model are
    // a natural follow-up once that actually comes up.
    bool PlayAnimationOnEntity(Entity targetEntity, const std::string& absoluteAnimationGtaPath);

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

    // Lazily decodes/uploads (once per distinct Guid, then cached in
    // m_materialTextureCache) a PMX material's diffuse texture into a
    // MaterialTexture (Renderer/MaterialTexture.h) - shared across every
    // submesh/model that happens to reference the exact same texture
    // asset. Resolves `textureGuid` through `database` (an AssetDatabase
    // freshly scanned over the mesh's own directory - see
    // EnsureMeshAsset()) into that Texture *.gta's absolute path, reads it
    // via ReadGtaFile(), and decodes its KTX2 payload via
    // Ktx2Decoder.h's DecodeKtx2ToRgba8() - the exact same asset-by-Guid
    // resolution path the Editor's own Inspector preview
    // (AssetPreviewTexture) uses for a *.gta Texture asset, so a material's
    // texture is loaded through the SAME "asset manager resolves an id to
    // its actual location" convention as every other *.gta asset in this
    // engine, never a raw filesystem path baked into the mesh itself (see
    // MaterialTextureRef's own doc comment, MaterialData.h). Returns
    // kInvalidTextureHandle (never throws) if `textureGuid` is
    // Guid::Invalid(), isn't currently tracked by `database`, or fails to
    // decode (missing/corrupt/moved *.gta - see MaterialData::textures' own
    // doc comment for why this is expected to legitimately happen
    // sometimes).
    TextureHandle EnsureMaterialTexture(Renderer& renderer, const AssetDatabase& database, const Guid& textureGuid);

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

    // The CPU-side "bind pose + rig" data kept around (see
    // m_meshSkinningCache below) for a model that actually has skinning
    // data, so a LATER PlayAnimationOnEntity() call can re-skin it every
    // frame - built once by EnsureMeshAsset() alongside its GPU
    // MeshAssetParts above. `uvs` mirrors bindPositions.size() (zero-filled
    // where the source mesh has none) purely so the SAME skinned output can
    // be reformatted into either MeshVertex (untextured parts) or
    // MeshVertexUv (textured parts) with no extra branching at update time.
    struct SkinnedMeshData {
        std::vector<Vec3> bindPositions;
        std::vector<Vec3> bindNormals;
        std::vector<Vec2> uvs;
        std::vector<VertexSkinWeights> skinWeights;
        SkeletonData skeleton;
    };
    // Decodes (once per distinct `absoluteAnimationGtaPath`, then cached in
    // m_animationClipCache) a *.gta AssetType::Animation file's MotionData
    // payload. Returns nullptr (never throws) if the file doesn't resolve
    // to a valid, non-empty Animation *.gta - see PlayAnimationOnEntity()'s
    // own doc comment for the exact failure cases.
    const MotionData* EnsureAnimationClip(const std::string& absoluteAnimationGtaPath);

    // Advances every live SkeletalAnimator's own playback frame by
    // `deltaSeconds` (VMD's fixed 30fps grid, scaled by that animator's own
    // `speed`, looping back to frame 0 once `loop` is set and the clip's
    // own last bone-keyframe frame is passed), re-evaluates its model's
    // full bone pose (Animation/SkeletonPose.h's ComputeSkinningMatrices())
    // and CPU-skins its bind-pose vertex data (Animation/VertexSkinning.h's
    // SkinVertices()) accordingly, then re-uploads every one of that
    // model's mesh parts' GPU vertex buffers via Mesh::UpdateVertexData()
    // (see RenderSystem::TryGetMesh()) - called once per frame from
    // Update(), never per render target (unlike Render() below, which runs
    // once per visible Game/Scene view - a model's pose only needs
    // computing once per frame regardless of how many views display it).
    void UpdateSkeletalAnimators(double deltaSeconds);

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
    // ScenePanel's drag-and-drop target) only ever has a filesystem path in
    // hand, never a Guid - Game does use AssetDatabase now (see
    // EnsureMeshAsset()), just never any Editor-only code (see AGENTS.md,
    // Clean Architecture).
    std::unordered_map<std::string, std::vector<MeshAssetPart>> m_meshAssetCache;

    // Keyed by the texture's own Guid (see EnsureMaterialTexture() above) -
    // shared across every model/material that references the same texture
    // asset (e.g. a body texture reused by several materials in one .pmx),
    // regardless of which mesh's own local AssetDatabase scan first
    // resolved it.
    std::unordered_map<Guid, TextureHandle> m_materialTextureCache;

    // Keyed by absolute Mesh *.gta filesystem path (same convention as
    // m_meshAssetCache) - present ONLY for a model that actually has a
    // non-empty skeleton + a matching per-vertex skin-weight count (see
    // EnsureMeshAsset()'s own updated doc comment). This is the CPU-side
    // "bind pose + rig" data Game::UpdateSkeletalAnimators() re-skins from
    // every frame - never duplicated per spawned entity/instance, only per
    // distinct asset path (see SkeletalAnimator's own doc comment,
    // ECS/Components/SkeletalAnimator.h, for the one documented consequence
    // of this sharing).
    std::unordered_map<std::string, SkinnedMeshData> m_meshSkinningCache;

    // Keyed by absolute Animation *.gta filesystem path - decoded once (see
    // EnsureAnimationClip()) and reused by every SkeletalAnimator that
    // references the same clip.
    std::unordered_map<std::string, MotionData> m_animationClipCache;

    // Keyed by "<meshGtaPath>\x1F<animationGtaPath>" (see
    // ResolvedAnimationBindingCacheKey()) - the bone-NAME resolution between
    // one specific model's skeleton and one specific motion's own bone
    // tracks (see Animation/MotionSampler.h's ResolveBoneTracksToSkeleton())
    // is computed once per distinct pair, then reused every frame
    // afterwards by every SkeletalAnimator playing that same combination.
    std::unordered_map<std::string, ResolvedAnimationBinding> m_resolvedAnimationBindingCache;
};

} // namespace gte
