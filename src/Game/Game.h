#pragma once

#include "../Core/GraphicsHandles.h"

#include "../Event/Event.h"
#include "../Input/InputState.h"
#include "../Core/EngineContext.h"
#include "Animation/AnimationSystem.h"
#include "ECS/Components/DirectionalLight.h"
#include "ECS/Registry.h"
#include "EngineCommandResults.h"
#include "Instantiation/MeshInstantiationSystem.h"
#include "Physics/PhysicsSystem.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"
#include "RenderSystem.h"
#include "../Core/FrameDebuggerCaptureRecorder.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace gte {

class Renderer;

// Composition root: owns the ECS registry plus the render, instantiation and animation systems.
// It only does entity/component lifecycle glue and forwards its public API to those systems.
class Game {
public:
    Game() = default;

    // Called once per discrete event before Update(); continuous input is read from Update()'s InputState.
    void OnEvent(const Event& event);

    // Runs every frame, paused or not; the freeze decision is made inside using the shared EngineContext.
    void Update(const EngineContext& engineContext, const InputState& input);

    // Queues this frame's draws for entities with a MeshRenderer. `viewProjectionOverride` null = active ECS
    // Camera (Game view); else that matrix (Scene view). `frameDebuggerCapture`, `maxDrawCount`, `batchedEntities`
    // and `cmd` apply to the Game view only; `sceneServicesSet` to both. Does not present.
    void Render(Renderer& renderer, float aspectWidthOverHeight, const Mat4* viewProjectionOverride = nullptr,
        IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr,
        std::optional<std::size_t> maxDrawCount = std::nullopt,
        const std::unordered_set<Entity>& batchedEntities = {},
        DescriptorSetHandle sceneServicesSet = DescriptorSetHandle::Invalid,
        rg::CommandBuffer* cmd = nullptr);


    // ECS access for the Editor's Hierarchy/Inspector panels; non-const because the Inspector edits components in place.
    Registry& GetRegistry() noexcept { return m_registry; }

    // Access to the render system for GPU-driven batch collection and mesh/pipeline lookup.
    RenderSystem& GetRenderSystem() noexcept { return m_renderSystem; }

    // Editor access to dynamic chain physics (detected chains/joints, per-joint settings, global settings).
    PhysicsSystem& GetPhysicsSystem() noexcept { return m_physicsSystem; }

    // Editor access to the cached mesh-asset catalog (e.g. to refresh saved joint physics overrides).
    MeshInstantiationSystem& GetMeshInstantiationSystem() noexcept { return m_meshInstantiationSystem; }

    // Spawns an entity with an identity Transform and a MeshRenderer for a built-in primitive shape.
    // `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set; it has no default
    // so a missed caller fails to compile.
    Entity CreatePrimitiveEntity(Renderer& renderer, PrimitiveType type, DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Spawns a "Directional Light" entity (Transform with a late-afternoon default rotation + DirectionalLight).
    // Needs no Renderer; the name is de-duplicated like InstantiatePrimitive() does.
    Entity CreateDirectionalLightEntity();


    // Spawns a *.gta mesh asset: an empty root named after the file plus one child per part; skinned models
    // go to the animation system. Returns the root, or kInvalidEntity (nothing created, never throws) if the
    // file is missing, corrupt or empty. `sceneServicesSetLayout` is Core's scene-services set layout.
    Entity CreateMeshEntityFromGtaFile(
        Renderer& renderer, const std::string& absoluteGtaPath, DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Outcome-returning wrapper around CreateMeshEntityFromGtaFile(): `success == false` with an error
    // message (and no entity) exactly when that call would return kInvalidEntity. Never throws.
    // `sceneServicesSetLayout` is the layout of Core's scene-services descriptor set.
    InstantiateMeshAssetOutcome InstantiateMeshAssetFromGtaFile(
        Renderer& renderer, const std::string& absoluteGtaPath, DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Plays `absoluteAnimationGtaPath` on `targetEntity`'s model (forwards to AnimationSystem::Play()).
    // Returns false, changing nothing, if the entity has no MeshAssetSource or registered skinning data,
    // or the path is not a valid *.gta animation. Never throws.
    bool PlayAnimationOnEntity(Entity targetEntity, const std::string& absoluteAnimationGtaPath);

    // Forwards to AnimationSystem's CPU/GPU skinning mode switch.
    void SetSkinningMode(AnimationSystem::SkinningMode mode) noexcept { m_animationSystem.SetSkinningMode(mode); }
    AnimationSystem::SkinningMode GetSkinningMode() const noexcept { return m_animationSystem.GetSkinningMode(); }

    // Called once per frame after Update(); see AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame().
    std::vector<AnimationSystem::GpuSkinningDispatchRequest> CollectGpuSkinningDispatchRequests() const
    {
        return m_animationSystem.CollectModelsNeedingGpuSkinningThisFrame();
    }

    // The shared compute-pipeline pair the GPU skinning passes dispatch against.
    GpuSkinningPipelines& GetGpuSkinningPipelines() noexcept { return m_animationSystem.GetGpuSkinningPipelines(); }

    // Number of entities with a MeshRenderer. Safe to call while declaring render graph passes (reads frozen ECS data).
    // Equals capture.DrawRecords().size() only while every MeshRenderer's mesh/pipeline handle resolves;
    // a skipped draw would shift the replay passes' maxDrawCount cutoffs out of line with the records.
    std::size_t CountGameViewDrawCommandsThisFrame()
    {
        return RenderSystem::CollectRenderables(m_registry).size();
    }

    // Spawns a primitive by shape name (case-insensitive) with a unique name and world position, optionally
    // parented by name. Unknown shape: no entity created. Unknown parent: entity left unparented, flagged
    // in `parentRequestedButNotFound`. Never throws. `sceneServicesSetLayout` is Core's scene-services set layout.
    InstantiatePrimitiveOutcome InstantiatePrimitive(Renderer& renderer, const std::string& shapeName,
        const std::string& requestedName, const Vec3& worldPosition, bool hasParent, const std::string& parentName,
        DescriptorSetLayoutHandle sceneServicesSetLayout);

    // Destroys the live entity named exactly `name`, plus all its descendants. Fails with nothing destroyed
    // if `name` is empty or unknown. With duplicate names, the first match in dense iteration order is destroyed.
    DeleteEntityOutcome DeleteEntityByName(const std::string& name);

    // Sets the LOCAL (parent-relative) Transform of the live entity named `params.name`, with no
    // hierarchy-aware world conversion. Each has* flag is applied independently; all false changes nothing.
    // Fails (nothing changed) if the name is empty/unknown (entityNotFound) or the entity has no Transform.
    SetEntityTrsOutcome SetEntityTrs(const SetEntityTrsParams& params);

    // Spawns a light entity from parameters (Transform + DirectionalLight); needs no Renderer. Only
    // lightType "directional" (case-insensitive, empty = directional) is supported; anything else fails
    // with no entity created. Without an explicit rotation it uses the same default as CreateDirectionalLightEntity().
    InstantiateLightOutcome InstantiateLight(const InstantiateLightParams& params);

    // Auto-creates the default Camera (back along -Z) when the live Camera component count is zero.
    // Called every frame by Render(); public so scene load/clear can restore it too.
    void EnsureDefaultCameraExists();

    // Frees GPU materials and meshes no entity or cached asset uses; cheap when nothing is pending.
    // Returns the freed meshes. Main thread, before any pass of the frame is recorded.
    std::vector<MeshHandle> ReleaseUnusedGpuResources(Renderer& renderer);

private:
    Registry m_registry;
    RenderSystem m_renderSystem;

    // Constructor-injected with m_renderSystem, so declared after it.
    MeshInstantiationSystem m_meshInstantiationSystem{ m_renderSystem };
    AnimationSystem m_animationSystem{ m_renderSystem, m_meshInstantiationSystem };

    // Runs between the animation system's pose evaluation and skin upload each frame; only touches the Registry.
    PhysicsSystem m_physicsSystem;

    std::uint64_t m_gpuSweepFrame = 0;
};

} // namespace gte
