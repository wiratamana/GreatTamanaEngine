#pragma once

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "Animation/AnimationSystem.h"
#include "ECS/Components/DirectionalLight.h"
#include "ECS/Registry.h"
#include "EngineCommandResults.h"
#include "Instantiation/MeshInstantiationSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "RenderSystem.h"

#include <string>

namespace gte {

class Renderer;

// Sits on top of Window/Renderer and has no direct knowledge of SDL, or of
// Vulkan beyond the Renderer abstraction. Game is a thin COMPOSITION ROOT:
// it owns the ECS World (m_registry) plus three collaborating systems -
// RenderSystem (drawing), MeshInstantiationSystem (spawning primitives/
// imported meshes), and AnimationSystem (playing back skeletal animation) -
// and does nothing itself beyond entity/component lifecycle glue
// (OnEvent/Update/Render) and one-line forwarding methods for its public
// API. Game never touches a Mesh/Pipeline/Vulkan/skinning-cache detail
// itself - all of the "how" lives in those three systems (and the smaller
// pure modules they're built from - EntityBlueprint/EntityInstantiator,
// MeshVertexPacking, MeshMaterialPartitioner, the Gpu catalogs, and the
// three animation caches - see src/Game/*.h); Game only ever creates
// entities/components indirectly through them and calls RenderSystem::Draw().
// See GameInstantiationRefactorProposal.txt for the full design rationale
// behind this split.
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

    // Editor-facing accessor (PHASE4,
    // task_manager/verlet-integration-1/PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md,
    // 3.5) - mirrors GetRegistry()'s own "Editor observes/edits through a
    // public accessor" convention, this time for the Inspector's "Dynamic
    // Chain Physics" section (Panels/InspectorPanel.cpp) to read detected
    // chain/joint counts, live-edit per-joint DynamicJointSettings, and show
    // the current (read-only) GlobalPhysicsSettings.
    PhysicsSystem& GetPhysicsSystem() noexcept { return m_physicsSystem; }

    // Editor-facing accessor (task_manager/verlet-integration-11, PHASE3) -
    // mirrors GetPhysicsSystem()'s own "Editor observes/acts through a public
    // accessor" convention, this time so the Inspector's "Save Joint Physics to
    // Asset" button (PHASE4) can keep THIS SESSION'S already-cached
    // SkinnedMeshData in sync with whatever it just wrote to disk - see
    // MeshInstantiationSystem::RefreshCachedJointPhysicsOverridesFromDisk()'s own
    // doc comment for the full contract this forwards to.
    MeshInstantiationSystem& GetMeshInstantiationSystem() noexcept { return m_meshInstantiationSystem; }

    // Spawns a new entity built from one of the engine's built-in primitive
    // shapes (PrimitiveType - see Renderer/Primitives/
    // PrimitiveMeshGenerator.h): a Transform at the origin (identity
    // rotation/scale) plus a MeshRenderer referencing that shape's mesh -
    // this engine's equivalent of Unity's GameObject.CreatePrimitive(), and
    // deliberately a RUNTIME/Game-level operation rather than an Editor-only
    // one (a real game could call this to spawn primitives too, exactly like
    // Unity's own API is not editor-only). The Editor's "Hierarchy" right-
    // click "Create 3D Object" menu (src/Editor/Panels/HierarchyPanel.cpp)
    // is just the first caller of it. A one-line forward into
    // MeshInstantiationSystem::SpawnPrimitive() - see that class (and
    // PrimitiveGpuCatalog/EntityInstantiator, which it's built from) for the
    // actual GPU-mesh-caching/entity-spawning behavior, unchanged from
    // before this refactor.
    Entity CreatePrimitiveEntity(Renderer& renderer, PrimitiveType type);

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 8
    // (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
    // - spawns a new "Sun" entity: a Transform (given a sensible default
    // rotation looking somewhat downward, like a late-afternoon sun) plus a
    // DirectionalLight component - this engine's equivalent of
    // CreatePrimitiveEntity() above, just with no Renderer/GPU mesh
    // involved at all (a light has nothing to rasterize). Needs no
    // `Renderer&` parameter for exactly that reason. Given a Name of
    // "Directional Light", auto-de-duplicated the same Unity-style way
    // InstantiatePrimitive() already does for primitives (see
    // ECS/EntityQuery.h's MakeUniqueEntityName()). The Editor's "Hierarchy"
    // right-click "Create Directional Light" entry
    // (src/Editor/Panels/HierarchyPanel.cpp) is the first caller of it.
    Entity CreateDirectionalLightEntity();


    // Spawns a whole hierarchy of entities from an imported *.gta
    // AssetType::Mesh file (see src/Assets/MeshFile.h/PmxLoader.h - the
    // result of importing a MikuMikuDance .pmx model via the Editor's
    // "Project" panel) at `absoluteGtaPath` - this engine's answer to "drag
    // a model asset into Hierarchy/Scene to instantiate it", the Mesh-asset
    // equivalent of CreatePrimitiveEntity() above.
    //
    // A multi-part model is spawned as a real parent/child hierarchy
    // (ECS/TransformHierarchy.h/Transform.h), not a flat list of independent
    // siblings: ONE plain, empty ROOT entity (an identity Transform only -
    // no MeshRenderer of its own, so it never renders anything by itself)
    // is created first, named after the asset FILE itself (`absoluteGtaPath`'s
    // own filename, minus its ".gta" extension - e.g. "Miku.gta" spawns a
    // root named "Miku"), and every part entity is attached under it - moving/
    // rotating/scaling the root moves the WHOLE model together, exactly like
    // dragging a multi-material FBX/glTF model into a Unity scene creates one
    // root GameObject with a child per submesh.
    //
    // A one-line forward into MeshInstantiationSystem::SpawnMeshAsset() (see
    // that class, and MeshAssetGpuCatalog/EntityBlueprint/EntityInstantiator,
    // which it's built from, for the full behavior this preserves exactly -
    // material/texture partitioning, skinned-vs-static mesh upload, per-part
    // naming, etc).
    //
    // Returns kInvalidEntity (never throws, and creates NO entities at all)
    // if `absoluteGtaPath` doesn't currently resolve to a valid, non-empty
    // *.gta AssetType::Mesh file (missing file, bad magic/wrong asset type,
    // corrupt/truncated payload, or zero vertices/triangles) - the caller
    // (HierarchyPanel/ScenePanel's drag-and-drop target) should simply
    // ignore the drop in that case, same "degrade gracefully" convention as
    // every other Editor drag-and-drop path (see AGENTS.md, "Editor Module
    // Structure"). Otherwise returns the newly created ROOT entity.
    //
    // If the freshly-spawned model turns out to be SKINNED (has both a real
    // bone hierarchy and matching per-vertex skin weights),
    // MeshInstantiationSystem::TryGetSkinnedMeshData() is used right here to
    // explicitly hand that data off to m_animationSystem's SkeletalRigCache
    // (see AnimationSystem::RegisterSkinnedMesh()) - the one, real,
    // visible-in-code wiring step that replaces the old hidden
    // "EnsureMeshAsset() writes a private Game member, UpdateSkeletalAnimators()
    // reads it" coupling (see GameInstantiationRefactorProposal.txt, Step
    // 2.4/3.5).
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
    // actually animate instead of always rendering its original bind pose.
    //
    // A one-line forward into AnimationSystem::Play() - see that class for
    // the full "how a bone/weight mismatch between the model and the motion
    // is handled" behavior this preserves exactly (bone-NAME resolution via
    // Animation/MotionSampler.h's ResolveBoneTracksToSkeleton(), tolerant of
    // a mismatch in either direction), and for the same documented
    // known-limitation this refactor is NOT expected to fix (two
    // simultaneously-animated instances of the same model *.gta still share
    // - and fight over - the same underlying GPU mesh buffers).
    //
    // Returns false (no component added/changed) if `targetEntity` isn't a
    // live entity with a MeshAssetSource component, its own model has no
    // skinning data registered with AnimationSystem at all (a boneless
    // mesh, one imported before rig extraction existed, or one whose
    // skinned data was never handed off - see CreateMeshEntityFromGtaFile()
    // above), or `absoluteAnimationGtaPath` doesn't currently resolve to a
    // valid, non-empty *.gta AssetType::Animation file - same "degrade
    // gracefully, never throw" convention as CreateMeshEntityFromGtaFile().
    bool PlayAnimationOnEntity(Entity targetEntity, const std::string& absoluteAnimationGtaPath);

    // GPU Vertex Skinning campaign, Phase 5 (Runtime CPU/GPU Switch - see
    // task_manager/gpu_skinning/GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md).
    // One-line forwards into AnimationSystem, mirroring every other public
    // method on this class - see AnimationSystem::SkinningMode/
    // SetSkinningMode()/GetSkinningMode()'s own doc comments
    // (Animation/AnimationSystem.h) for the full behavior.
    void SetSkinningMode(AnimationSystem::SkinningMode mode) noexcept { m_animationSystem.SetSkinningMode(mode); }
    AnimationSystem::SkinningMode GetSkinningMode() const noexcept { return m_animationSystem.GetSkinningMode(); }

    // Called once per frame by src/Application/RenderPasses.cpp's
    // AddGpuSkinningPasses(), AFTER Game::Update() has already run this
    // frame - see AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame().
    std::vector<AnimationSystem::GpuSkinningDispatchRequest> CollectGpuSkinningDispatchRequests() const
    {
        return m_animationSystem.CollectModelsNeedingGpuSkinningThisFrame();
    }

    // The shared compute-pipeline pair AddGpuSkinningPasses() dispatches
    // against - see AnimationSystem::GetGpuSkinningPipelines()'s own doc
    // comment.
    GpuSkinningPipelines& GetGpuSkinningPipelines() noexcept { return m_animationSystem.GetGpuSkinningPipelines(); }

    // Spawns a primitive shape by NAME (network-impl-3 campaign) - the same
    // underlying spawn as CreatePrimitiveEntity() above (shares its GPU mesh/
    // pipeline cache, PrimitiveGpuCatalog - a second "cube" spawned this way
    // costs no new GPU upload), plus three additive steps: (1) `shapeName` is
    // parsed case-insensitively via PrimitiveMeshGenerator::TryParsePrimitiveTypeName() -
    // an unrecognized shape name fails the whole call (no entity created) with
    // `errorMessage` explaining which names ARE valid; (2) `requestedName`
    // (falling back to the shape's own ToString() when empty - e.g. an
    // unqualified "Cube" for a plain cube spawn with no name given) is passed
    // through EntityQuery.h's MakeUniqueEntityName() and the RESULT is what
    // actually gets added as this entity's Name component - Unity's own
    // "GameObject", "GameObject (1)", ... auto-de-duplication behavior (see
    // AGENTS.md/network-impl-3's PHASE0's own Locked Design Decision #3);
    // (3) the entity's Transform::position is set to `worldPosition` (valid
    // since a freshly spawned, still-unparented entity's local position IS
    // its world position - see ECS/Components/Transform.h), and if
    // `hasParent` is true and `parentName` resolves (via EntityQuery.h's
    // FindEntityByName()) to a live entity, ECS/TransformHierarchy.h's
    // SetParent(..., worldPositionStays = true) attaches it there, preserving
    // the exact world position just set. If `hasParent` is true but
    // `parentName` does NOT resolve to a live entity, the entity is still
    // created, left UNPARENTED (world space) -
    // InstantiatePrimitiveOutcome::parentRequestedButNotFound is set to true
    // and `requestedParentName` echoes `parentName` back, but this is never
    // treated as a failure of the overall call (see network-impl-3's Locked
    // Design Decision #2).
    //
    // Never throws. Returns an outcome with success == false (and creates NO
    // entity at all) only for an unrecognized `shapeName`.
    InstantiatePrimitiveOutcome InstantiatePrimitive(Renderer& renderer, const std::string& shapeName,
        const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName);

    // Destroys the live entity (and every descendant of it - see
    // ECS/TransformHierarchy.h::DestroyEntityAndDescendants()) whose Name
    // component value exactly equals `name` (network-impl-3 campaign).
    // Returns success == false (destroying NOTHING) if `name` is empty or no
    // live entity currently has that exact name. If more than one live
    // entity happens to share the same name (possible only through some
    // OTHER naming path than InstantiatePrimitive() above, which always
    // auto-dedupes - see network-impl-3's Locked Design Decision #3), the
    // FIRST match found by EntityQuery.h::FindEntityByName()'s own
    // dense-iteration-order rule is the one destroyed - documented as
    // best-effort, matching FindEntityByName()'s own doc comment.
    DeleteEntityOutcome DeleteEntityByName(const std::string& name);

private:
    // The engine's one auto-created entity: a Camera sitting back along -Z
    // so a brand-new (or freshly-loaded - see task_manager/
    // scene-serialization-1/) scene always has something to actually look
    // through in "Scene"/"Game" - see this method's own doc comment in
    // Game.cpp for the full rationale. Used to also build 3 hardcoded demo
    // triangles proving the ECS -> RenderSystem -> Renderer pipeline end to
    // end; that scaffolding is gone now that real scene content (primitives,
    // imported meshes, and a real save/load loop) exists instead (see
    // TODO.md's "Scene serialization" entry).
    void EnsureDefaultCameraExists();

    Registry m_registry;
    RenderSystem m_renderSystem;

    // Depends on m_renderSystem (constructor-injected reference) - declared
    // after it so it's already constructed by the time these run.
    MeshInstantiationSystem m_meshInstantiationSystem{ m_renderSystem };
    AnimationSystem m_animationSystem{ m_renderSystem, m_meshInstantiationSystem };

    // Phase 3 (task_manager/verlet-integration-1/
    // PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md, v3/v4) - a fourth,
    // fully independent Game-layer orchestrator, sandwiched between
    // m_animationSystem's EvaluatePoses()/SkinAndUpload() halves every frame
    // (see Update(), Game.cpp). Takes no constructor dependencies (unlike
    // AnimationSystem above) - it never touches Renderer/Mesh/RenderSystem/
    // MeshInstantiationSystem at all, only the ECS Registry.
    PhysicsSystem m_physicsSystem;

    // Kept only for EnsureDefaultCameraExists()'s own one-time guard above.
    bool m_defaultCameraEnsured = false;
};

} // namespace gte
