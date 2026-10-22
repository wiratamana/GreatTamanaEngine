#include "Game.h"

#include "../Renderer/Renderer.h"
#include "ECS/Components/Camera.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Name.h"
#include "ECS/Components/Transform.h"
#include "ECS/EntityQuery.h"
#include "Instantiation/MaterialLifetimeRules.h"
#include "ECS/TransformHierarchy.h"
#include "Profiling/ScopeTimer.h"

#include <algorithm>
#include <cctype>

namespace gte {

namespace {
// Late-afternoon sun rotation shared by CreateDirectionalLightEntity() and InstantiateLight().
Quat DefaultDirectionalLightRotation() noexcept
{
    return Quat::FromEulerDegrees(45.0f, -30.0f, 0.0f);
}
} // namespace

void Game::OnEvent(const Event& /*event*/)
{
    // One-shot event handling goes here; event.type selects the active alternative of event.data.
}

void Game::Update(const EngineContext& engineContext, const InputState& /*input*/)
{
    GTE_PROFILE_SCOPE("Game::Update");

    // Game/simulation logic goes here; poll `input` for continuous state.

    const Time& time = engineContext.time;

    // Frozen = paused and not stepping. Skip the simulation stages entirely rather than feeding them a zero
    // delta: cheaper, and independent of each system's zero-delta handling.
    if (!time.IsFrozenThisFrame()) {
        // Three stages sharing only the ResolvedAnimationPose component: animation writes the pose, physics may
        // overwrite physics-driven bones, then animation skins and uploads whatever the pose holds.
        m_animationSystem.EvaluatePoses(m_registry, time.DeltaTime());
        m_physicsSystem.Update(m_registry, time.DeltaTime());
        m_animationSystem.SkinAndUpload(m_registry);
    } else {
        // Nothing simulated: drop any stale GPU-skinning dispatch request from the last simulated frame.
        m_animationSystem.ClearGpuSkinningDispatchThisFrame();
    }
}

Entity Game::CreatePrimitiveEntity(Renderer& renderer, PrimitiveType type, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    return m_meshInstantiationSystem.SpawnPrimitive(m_registry, renderer, type, sceneServicesSetLayout);
}

Entity Game::CreateDirectionalLightEntity()
{
    const Entity entity = m_registry.CreateEntity();

    Transform& transform = m_registry.AddComponent<Transform>(entity);
    transform.rotation = DefaultDirectionalLightRotation();

    m_registry.AddComponent<DirectionalLight>(entity);

    const std::string uniqueName = MakeUniqueEntityName(m_registry, "Directional Light");
    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    return entity;
}

Entity Game::CreateMeshEntityFromGtaFile(
    Renderer& renderer, const std::string& absoluteGtaPath, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    const Entity root = m_meshInstantiationSystem.SpawnMeshAsset(m_registry, renderer, absoluteGtaPath, sceneServicesSetLayout);
    if (root != kInvalidEntity) {
        // Explicit hand-off: a skinned model's bind-pose/rig data is registered with AnimationSystem here.
        if (const SkinnedMeshData* skin = m_meshInstantiationSystem.TryGetSkinnedMeshData(absoluteGtaPath)) {
            m_animationSystem.RegisterSkinnedMesh(absoluteGtaPath, *skin);

            // Physics-side registration, alongside (never through) the animation registration above.
            m_physicsSystem.RegisterDynamicChains(absoluteGtaPath, *skin);
            m_physicsSystem.AttachDynamicChainRigIfNeeded(m_registry, root, absoluteGtaPath);

            // Registered unconditionally (never lazily), whichever skinning mode is active.
            if (const std::vector<MeshAssetPart>* parts = m_meshInstantiationSystem.TryGetMeshAssetParts(absoluteGtaPath)) {
                m_animationSystem.RegisterGpuSkinnedMesh(renderer, absoluteGtaPath, *skin, *parts);
            }
        }
    }
    return root;
}

InstantiateMeshAssetOutcome Game::InstantiateMeshAssetFromGtaFile(
    Renderer& renderer, const std::string& absoluteGtaPath, DescriptorSetLayoutHandle sceneServicesSetLayout)
{
    InstantiateMeshAssetOutcome outcome;

    const Entity root = CreateMeshEntityFromGtaFile(renderer, absoluteGtaPath, sceneServicesSetLayout);
    if (root == kInvalidEntity) {
        outcome.success = false;
        outcome.errorMessage = "failed to load or spawn a Mesh asset from \"" + absoluteGtaPath
            + "\" - the file may be missing, not a valid Mesh *.gta, or empty (zero vertices/triangles)";
        return outcome;
    }

    outcome.success = true;
    outcome.entityIndex = root.index;
    outcome.entityGeneration = root.generation;
    if (const Name* name = m_registry.TryGetComponent<Name>(root)) {
        outcome.resolvedName = name->value;
    }
    return outcome;
}

bool Game::PlayAnimationOnEntity(Entity targetEntity, const std::string& absoluteAnimationGtaPath)
{
    return m_animationSystem.Play(m_registry, targetEntity, absoluteAnimationGtaPath);
}

InstantiatePrimitiveOutcome Game::InstantiatePrimitive(Renderer& renderer, const std::string& shapeName,
    const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName,
    DescriptorSetLayoutHandle sceneServicesSetLayout)
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

    const Entity entity = CreatePrimitiveEntity(renderer, type, sceneServicesSetLayout);
    // Defensive: never report success with a bogus entity handle.
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

SetEntityTrsOutcome Game::SetEntityTrs(const SetEntityTrsParams& params)
{
    SetEntityTrsOutcome outcome;

    const Entity entity = FindEntityByName(m_registry, params.name);
    if (entity == kInvalidEntity) {
        outcome.success = false;
        outcome.entityNotFound = true;
        outcome.errorMessage = params.name.empty()
            ? "name must not be empty"
            : ("no live entity found with name '" + params.name + "'");
        return outcome;
    }

    Transform* transform = m_registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        outcome.success = false;
        outcome.errorMessage = "entity '" + params.name + "' has no Transform component";
        return outcome;
    }

    if (params.hasTranslation) {
        transform->position = params.translation;
        outcome.translationChanged = true;
    }
    if (params.hasRotationEulerDegrees) {
        transform->rotation = Quat::FromEulerDegrees(
            params.rotationEulerDegrees.x, params.rotationEulerDegrees.y, params.rotationEulerDegrees.z);
        outcome.rotationChanged = true;
    }
    if (params.hasScale) {
        transform->scale = params.scale;
        outcome.scaleChanged = true;
    }

    outcome.success = true;
    outcome.entityIndex = entity.index;
    outcome.entityGeneration = entity.generation;
    outcome.resultingPosition = transform->position;
    outcome.resultingRotation = transform->rotation;
    outcome.resultingScale = transform->scale;
    return outcome;
}

InstantiateLightOutcome Game::InstantiateLight(const InstantiateLightParams& params)
{
    InstantiateLightOutcome outcome;

    std::string normalizedType = params.lightType;
    std::transform(normalizedType.begin(), normalizedType.end(), normalizedType.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!normalizedType.empty() && normalizedType != "directional") {
        outcome.success = false;
        outcome.errorMessage = "unsupported light_type '" + params.lightType
            + "' - only 'directional' is currently supported";
        return outcome;
    }

    const Entity entity = m_registry.CreateEntity();

    Transform& transform = m_registry.AddComponent<Transform>(entity);
    transform.position = params.worldPosition;
    transform.rotation = params.hasRotationEulerDegrees
        ? Quat::FromEulerDegrees(params.rotationEulerDegrees.x, params.rotationEulerDegrees.y, params.rotationEulerDegrees.z)
        : DefaultDirectionalLightRotation();

    DirectionalLight& light = m_registry.AddComponent<DirectionalLight>(entity);
    light.color = params.color;
    light.illuminanceLux = params.illuminanceLux;
    light.active = params.active;

    const std::string baseName = params.requestedName.empty() ? std::string("Directional Light") : params.requestedName;
    const std::string uniqueName = MakeUniqueEntityName(m_registry, baseName);
    m_registry.AddComponent<Name>(entity, Name{ uniqueName });

    outcome.success = true;
    outcome.entityIndex = entity.index;
    outcome.entityGeneration = entity.generation;
    outcome.resolvedName = uniqueName;

    if (params.hasParent) {
        const Entity parentEntity = FindEntityByName(m_registry, params.parentName);
        if (parentEntity == kInvalidEntity) {
            outcome.parentRequestedButNotFound = true;
            outcome.requestedParentName = params.parentName;
        } else {
            SetParent(m_registry, entity, parentEntity, /*worldPositionStays=*/true);
        }
    }

    return outcome;
}

void Game::EnsureDefaultCameraExists()
{
    // Live check (not a one-shot flag): a scene clear or a load with no Camera must self-heal.
    if (m_registry.Storage<Camera>().Size() > 0) {
        return; // A camera already exists (default OR loaded from a scene) - nothing to do.
    }

    // Default camera: back along -Z (identity rotation looks down +Z) so a new or freshly loaded scene
    // always has something to look through.
    const Entity cameraEntity = m_registry.CreateEntity();
    Transform& cameraTransform = m_registry.AddComponent<Transform>(cameraEntity);
    cameraTransform.position = Vec3{ 0.0f, 0.0f, -5.0f };
    m_registry.AddComponent<Camera>(cameraEntity);
}

void Game::Render(Renderer& renderer, float aspectWidthOverHeight, const Mat4* viewProjectionOverride,
    IFrameDebuggerCaptureRecorder* frameDebuggerCapture, std::optional<std::size_t> maxDrawCount,
    const std::unordered_set<Entity>& batchedEntities, DescriptorSetHandle sceneServicesSet, rg::CommandBuffer* cmd)
{
    EnsureDefaultCameraExists();

    if (viewProjectionOverride != nullptr) {
        // Scene view: no capture, draw cap or command buffer; claimed entities are skipped like in the Game view.
        m_renderSystem.Draw(m_registry, renderer, *viewProjectionOverride, nullptr, std::nullopt, batchedEntities,
            std::nullopt, sceneServicesSet);
    } else {
        // Capture pointer and `cmd` are only forwarded, never dereferenced here.
        m_renderSystem.Draw(m_registry, renderer, aspectWidthOverHeight, frameDebuggerCapture, maxDrawCount,
            batchedEntities, sceneServicesSet, cmd);
    }
}

std::vector<MeshHandle> Game::ReleaseUnusedGpuResources(Renderer& renderer)
{
    if (!ShouldRunMaterialSweep(m_meshInstantiationSystem.ReleasePending(), ++m_gpuSweepFrame)) {
        return {};
    }
    std::vector<MeshHandle> skinningBindings;
    m_animationSystem.AppendGpuSkinningMeshHandles(skinningBindings);
    return m_meshInstantiationSystem.ReleaseUnusedGpuResources(m_registry, renderer, skinningBindings);
}

} // namespace gte
