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

// The Project Assembly on-screen render-feature compositor: owns the
// PostComposite/PreUI blend chain and the PreOpaque/PostOpaque/
// PostTransparent registration lists. Public, zero Vulkan dependency - every
// Vulkan-bearing field/helper lives in a private Impl, defined in
// Renderer/Internal/RenderFeatureCompositorImpl.h (Renderer-internal only).
namespace gte {

class Core;     // Forward declaration only - avoids a circular include with Core.h.
class Renderer; // Forward declaration only - held as a plain reference member.

class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    RenderFeatureCompositor(Core& core, Renderer& renderer);
    ~RenderFeatureCompositor() override;

    // Intentionally empty - IPluginCapabilityOrchestrator requires an
    // override but nothing calls this anymore.
    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // Read-only snapshot of the real, resolved ordering decision - walks
    // PostComposite then PreUI, the same order ContributeRenderGraphPasses()
    // uses. Consumed by the Editor's "Render Graph" panel. Safe to call at
    // most once per Editor frame; never mutates state.
    std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;

    // Host-side enable/disable override. Finds `name` in any registered
    // list and sets its enabledOverride. Returns false if `name` matches no
    // registered feature.
    bool SetFeatureEnabled(const std::string& name, bool enabled);

    // Host-side live priority override. Mutates the stored priority, then
    // re-sorts and re-runs collision detection for only the one stage
    // `name` belongs to. Returns false if `name` matches no registered feature.
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);

    // One real registration entry point - owner is explicit, never inferred.
    // `descriptor` must already be built via MakeRenderFeatureDescriptor().
    // Returns false (logged, never crashes) if the name already exists, if
    // descriptor.stage is not wired in this engine build, or if every
    // project-feature GPU-state slot is claimed.
    bool RegisterFeature(const GtePluginRenderFeatureDescriptor& descriptor,
        ProjectRenderFeatureCallback callback, RenderFeatureOwner owner);

    // Thin, owner-fixed wrappers - callers always use one of these, never
    // RegisterFeature() directly.
    bool RegisterProjectFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback)
    {
        return RegisterFeature(descriptor, std::move(callback), RenderFeatureOwner::Project);
    }
    bool RegisterBuiltInFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback)
    {
        return RegisterFeature(descriptor, std::move(callback), RenderFeatureOwner::Engine);
    }

    // Removes a previously-registered project feature by name, releasing its
    // claimed GPU-state slot back to the free list. Returns false if no
    // entry with that name exists, or if it is not a project feature.
    bool UnregisterProjectFeature(const char* name);

    // A Project Assembly's own PreOpaque feature. Not a reuse of Entry -
    // PreOpaque has no GPU-state slot, no blend mode, no private target: it
    // draws into its own render view and publishes directly.
    struct PreOpaqueEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectPreOpaqueCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // PreOpaque sibling of RegisterProjectFeature(). Returns false (logged)
    // if `name` is already registered under any stage - all feature names
    // share one global namespace.
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

    // The new "PreOpaqueFeatures" RenderPipeline provider walks this
    // accessor, in order, skipping any entry whose enabledOverride is
    // currently false. Already sorted by priority ascending.
    const std::vector<PreOpaqueEntry>& PreOpaqueFeaturesInPriorityOrder() const noexcept;

    // The PostOpaque sibling of PreOpaqueEntry above - a Project Assembly
    // callback guaranteed to run after the current view's own opaque pass
    // and before its own transparent pass. Shares ProjectScenePassCallback
    // with PostTransparentEntry below.
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

    // The PostTransparent sibling of PostOpaqueEntry above - guaranteed to
    // run after the current view's own transparent pass.
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
    // Real registration bodies - owner is explicit, never inferred. Public
    // RegisterXFeature()/RegisterBuiltInXFeature() above are thin,
    // owner-fixed wrappers.
    bool RegisterPreOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner);
    bool RegisterPostOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);
    bool RegisterPostTransparentFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);

    // A Project Assembly's own on-screen render feature (PostComposite/
    // PreUI) - Vulkan-free too (callback/bool/int/enum fields only), stays
    // here for the same reason as the 3 structs above: Impl's own private
    // containers reference this type by name rather than redeclaring it.
    struct Entry {
        GtePluginRenderFeatureDescriptor descriptor{};
        bool enabledOverride = true;
        ProjectRenderFeatureCallback projectCallback;
        int projectFeatureSlot = -1;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // Stable sort by priority, logged lexical tie-break on collision -
    // Vulkan-free, operate only on the structs above.
    static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);
    static void SortAndDetectCollisionsInPreOpaqueList(std::vector<PreOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostOpaqueList(std::vector<PostOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostTransparentList(std::vector<PostTransparentEntry>& entries);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
