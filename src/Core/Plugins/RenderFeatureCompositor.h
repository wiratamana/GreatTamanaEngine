#pragma once

#include "IPluginCapabilityOrchestrator.h"
#include "ProjectRenderFeatureCallback.h"
#include "ProjectPreOpaqueCallback.h"
#include "ProjectScenePassCallback.h"
#include "RenderFeatureDebugEntry.h"
#include "RenderFeatureDescriptor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class Core;
class Renderer;

// Owns the PostComposite/PreUI blend chain and the PreOpaque/PostOpaque/PostTransparent registration lists.
// Its state lives in a private Impl (RenderFeatureCompositorImpl.h).
class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    RenderFeatureCompositor(Core& core, Renderer& renderer);
    ~RenderFeatureCompositor() override;

    // Required by the interface; nothing calls it.
    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // Read-only view of the resolved order (PostComposite, then PreUI) for the Render Graph panel.
    std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;

    // Sets the enabled override of `name` in any list. False when no feature has that name.
    bool SetFeatureEnabled(const std::string& name, bool enabled);

    // Changes the priority of `name` and re-sorts its stage. False when no feature has that name.
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);

    // `descriptor` must come from MakeRenderFeatureDescriptor(). False (logged) when the name exists, the stage
    // is not wired, or every project-feature slot is taken.
    bool RegisterFeature(const GtePluginRenderFeatureDescriptor& descriptor,
        ProjectRenderFeatureCallback callback, RenderFeatureOwner owner);

    // Owner-fixed wrappers over RegisterFeature().
    bool RegisterProjectFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback)
    {
        return RegisterFeature(descriptor, std::move(callback), RenderFeatureOwner::Project);
    }
    bool RegisterBuiltInFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback)
    {
        return RegisterFeature(descriptor, std::move(callback), RenderFeatureOwner::Engine);
    }

    // Removes a project feature and frees its GPU-state slot. False when absent or not a project feature.
    bool UnregisterProjectFeature(const char* name);

    // A PreOpaque feature: no slot, no blend mode, no private target; it draws into its own view.
    struct PreOpaqueEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectPreOpaqueCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // False (logged) when `name` exists in any stage: feature names share one namespace.
    bool RegisterPreOpaqueFeature(const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback)
    {
        return RegisterPreOpaqueFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Project);
    }
    bool RegisterBuiltInPreOpaqueFeature(
        const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback)
    {
        return RegisterPreOpaqueFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Engine);
    }
    bool UnregisterPreOpaqueFeature(const char* name);

    // Sorted by priority ascending; the PreOpaque provider skips entries whose override is off.
    const std::vector<PreOpaqueEntry>& PreOpaqueFeaturesInPriorityOrder() const noexcept;

    // Runs after the view's opaque pass and before its transparent pass.
    struct PostOpaqueEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectScenePassCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    bool RegisterPostOpaqueFeature(const std::string& name, std::int32_t priority, ProjectScenePassCallback callback)
    {
        return RegisterPostOpaqueFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Project);
    }
    bool RegisterBuiltInPostOpaqueFeature(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback)
    {
        return RegisterPostOpaqueFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Engine);
    }
    bool UnregisterPostOpaqueFeature(const char* name);
    const std::vector<PostOpaqueEntry>& PostOpaqueFeaturesInPriorityOrder() const noexcept;

    // Runs after the view's transparent pass.
    struct PostTransparentEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectScenePassCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    bool RegisterPostTransparentFeature(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback)
    {
        return RegisterPostTransparentFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Project);
    }
    bool RegisterBuiltInPostTransparentFeature(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback)
    {
        return RegisterPostTransparentFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Engine);
    }
    bool UnregisterPostTransparentFeature(const char* name);
    const std::vector<PostTransparentEntry>& PostTransparentFeaturesInPriorityOrder() const noexcept;

private:
    // The registration bodies; the public wrappers above fix the owner.
    bool RegisterPreOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner);
    bool RegisterPostOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);
    bool RegisterPostTransparentFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);

    // An on-screen feature (PostComposite or PreUI).
    struct Entry {
        GtePluginRenderFeatureDescriptor descriptor{};
        bool enabledOverride = true;
        ProjectRenderFeatureCallback projectCallback;
        int projectFeatureSlot = -1;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // Stable sort by priority; a collision logs a warning and falls back to a name tie-break.
    static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);
    static void SortAndDetectCollisionsInPreOpaqueList(std::vector<PreOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostOpaqueList(std::vector<PostOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostTransparentList(std::vector<PostTransparentEntry>& entries);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
