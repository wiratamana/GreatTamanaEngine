#include "Game.h"

#include "../Renderer/Renderer.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/Transform.h"
#include "ECS/EntityQuery.h"
#include "ECS/TransformHierarchy.h"
#include "Profiling/ScopeTimer.h"

namespace gte {

void Game::OnEvent(const Event& /*event*/)
{
    // Discrete/one-shot event handling goes here (react to a single key
    // press, window resized, etc). event.type tells you which alternative of
    // event.data is active - see Event.h.
}

void Game::Update(double deltaSeconds, const InputState& /*input*/)
{
    GTE_PROFILE_SCOPE("Game::Update");

    // Game/simulation logic goes here. Poll `input` for continuous state,
    // e.g. `if (input.IsKeyDown(KeyCode::W)) { ... }` for held-key movement.

    // Phase 3 (task_manager/verlet-integration-1/
    // PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md, v3/v4) - three
    // genuinely independent stages, communicating ONLY through the
    // ResolvedAnimationPose ECS component (see that component's own doc
    // comment): AnimationSystem samples/IK-solves/append-inherits and
    // writes a fresh pose; PhysicsSystem optionally overwrites individual
    // physics-driven bones in that SAME pose; AnimationSystem then skins and
    // uploads whatever the pose currently holds, regardless of which of the
    // two touched it last.
    m_animationSystem.EvaluatePoses(m_registry, deltaSeconds);
    m_physicsSystem.Update(m_registry, deltaSeconds);
    m_animationSystem.SkinAndUpload(m_registry);
}

Entity Game::CreatePrimitiveEntity(Renderer& renderer, PrimitiveType type)
{
    return m_meshInstantiationSystem.SpawnPrimitive(m_registry, renderer, type);
}

Entity Game::CreateDirectionalLightEntity()
{
    const Entity entity = m_registry.CreateEntity();

    Transform& transform = m_registry.AddComponent<Transform>(entity);
    // Late-afternoon-ish sun: pitched down toward the ground plus a bit of
    // yaw so it isn't perfectly axis-aligned - purely a sensible visual
    // default (see Quat::FromEulerDegrees()'s own "pitch around Right()"
    // convention), not physically derived.
    transform.rotation = Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f);

    m_registry.AddComponent<DirectionalLight>(entity);

    const std::string uniqueName = MakeUniqueEntityName(m_registry, "Directional Light");
    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    return entity;
}

Entity Game::CreateMeshEntityFromGtaFile(Renderer& renderer, const std::string& absoluteGtaPath)
{
    const Entity root = m_meshInstantiationSystem.SpawnMeshAsset(m_registry, renderer, absoluteGtaPath);
    if (root != kInvalidEntity) {
        // Explicit hand-off: if this model turned out to be skinned, make
        // its bind-pose/rig data available to AnimationSystem's own
        // SkeletalRigCache right here - a real, visible-in-code call site,
        // never an implicit shared-cache coupling (see this method's own
        // doc comment, Game.h).
        if (const SkinnedMeshData* skin = m_meshInstantiationSystem.TryGetSkinnedMeshData(absoluteGtaPath)) {
            m_animationSystem.RegisterSkinnedMesh(absoluteGtaPath, *skin);

            // Phase 3/4 (task_manager/verlet-integration-1/) - the physics-side
            // sibling of RegisterSkinnedMesh() above, called from the SAME
            // hand-off site, ALONGSIDE (never through) AnimationSystem's own
            // registration. PHASE3: both calls are provable no-ops (see
            // PhysicsSystem.cpp) - PHASE4 is what makes real chain detection
            // populate them.
            m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin);
            m_physicsSystem.AttachDynamicChainRigIfNeeded(m_registry, root, absoluteGtaPath);

            // GPU Vertex Skinning campaign, Phase 4 (Per-Model Resource
            // Management - see
            // task_manager/gpu_skinning/GPU_SKINNING_PHASE4_PER_MODEL_RESOURCE_MANAGEMENT_STRATEGY_v1.md).
            // Registered unconditionally, alongside RegisterSkinnedMesh()
            // above, regardless of which skinning mode is currently active -
            // see AnimationSystem::RegisterGpuSkinnedMesh()'s own doc
            // comment for why this must never become a lazy/on-first-use
            // registration.
            if (const std::vector<MeshAssetPart>* parts = m_meshInstantiationSystem.TryGetMeshAssetParts(absoluteGtaPath)) {
                m_animationSystem.RegisterGpuSkinnedMesh(renderer, absoluteGtaPath, *skin, *parts);
            }
        }
    }
    return root;
}

bool Game::PlayAnimationOnEntity(Entity targetEntity, const std::string& absoluteAnimationGtaPath)
{
    return m_animationSystem.Play(m_registry, targetEntity, absoluteAnimationGtaPath);
}

InstantiatePrimitiveOutcome Game::InstantiatePrimitive(Renderer& renderer, const std::string& shapeName,
    const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName)
{
    InstantiatePrimitiveOutcome outcome;

    PrimitiveType type{};
    if (!TryParsePrimitiveTypeName(shapeName, type)) {
        outcome.success = false;
        outcome.errorMessage = "unrecognized shape '" + shapeName
            + "' - expected one of: cube, sphere, capsule, cone, plane";
        return outcome;
    }

    const std::string baseName = requestedName.empty() ? std::string(ToString(type)) : requestedName;
    const std::string uniqueName = MakeUniqueEntityName(m_registry, baseName);

    const Entity entity = CreatePrimitiveEntity(renderer, type);
    // Defensive - CreatePrimitiveEntity() is not currently documented to ever
    // return kInvalidEntity, but this is cheap insurance against a future
    // change there (e.g. a GPU resource creation failure surfaced as
    // kInvalidEntity instead of an exception) silently producing a "success"
    // outcome with a bogus entity handle.
    if (entity == kInvalidEntity) {
        outcome.success = false;
        outcome.errorMessage = "failed to create primitive entity";
        return outcome;
    }

    Transform& transform = m_registry.GetComponent<Transform>(entity);
    transform.position = worldPosition;

    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    outcome.success = true;
    outcome.entityIndex = entity.index;
    outcome.entityGeneration = entity.generation;
    outcome.resolvedName = uniqueName;

    if (hasParent) {
        const Entity parentEntity = FindEntityByName(m_registry, parentName);
        if (parentEntity == kInvalidEntity) {
            outcome.parentRequestedButNotFound = true;
            outcome.requestedParentName = parentName;
        } else {
            SetParent(m_registry, entity, parentEntity, /*worldPositionStays=*/true);
        }
    }

    return outcome;
}

DeleteEntityOutcome Game::DeleteEntityByName(const std::string& name)
{
    DeleteEntityOutcome outcome;
    const Entity entity = FindEntityByName(m_registry, name);
    if (entity == kInvalidEntity) {
        outcome.success = false;
        outcome.errorMessage = name.empty()
            ? "name must not be empty"
            : ("no live entity found with name '" + name + "'");
        return outcome;
    }
    outcome.deletedEntityIndex = entity.index;
    outcome.deletedEntityGeneration = entity.generation;
    DestroyEntityAndDescendants(m_registry, entity);
    outcome.success = true;
    return outcome;
}

void Game::EnsureDefaultCameraExists()
{
    if (m_defaultCameraEnsured) {
        return;
    }
    m_defaultCameraEnsured = true;

    // The engine's one auto-created entity: a Camera sitting back along -Z
    // (an identity rotation looks straight down +Z - see
    // Camera::ViewMatrix(), ECS/Components/Camera.h) so a brand-new (or
    // freshly-loaded - see task_manager/scene-serialization-1/) scene always
    // has something to actually look through in "Scene"/"Game", instead of
    // falling back to RenderSystem::ResolveActiveCameraViewProjection()'s
    // Mat4::Identity() default. This is now the ONLY thing this function
    // does - it used to also build 3 hardcoded demo triangles proving the
    // ECS -> RenderSystem -> Renderer pipeline end to end; that scaffolding
    // is gone now that real scene content (primitives, imported meshes, and
    // - as of task_manager/scene-serialization-1/ - a real save/load loop)
    // exists instead. See TODO.md's "Scene serialization" entry.
    const Entity cameraEntity = m_registry.CreateEntity();
    Transform& cameraTransform = m_registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3{ 0.0f, 0.0f, -5.0f };
    m_registry.AddComponent<Camera>(cameraEntity);
}

void Game::Render(Renderer& renderer, float aspectWidthOverHeight, const Mat4* viewProjectionOverride)
{
    renderer.Clear(20, 20, 30, 255);

    EnsureDefaultCameraExists();

    if (viewProjectionOverride != nullptr) {
        m_renderSystem.Draw(m_registry, renderer, *viewProjectionOverride);
    } else {
        m_renderSystem.Draw(m_registry, renderer, aspectWidthOverHeight);
    }
}

} // namespace gte
