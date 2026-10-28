#pragma once

#include "IPluginCapabilityOrchestrator.h"
#include "ProjectRenderFeatureCallback.h"
#include "ProjectPreOpaqueCallback.h"
#include "ProjectScenePassCallback.h"
#include "RenderFeatureDebugEntry.h"
#include "RenderFeatureDescriptor.h"
#include "ViewScopeEnable.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace gte {

class Core;
class Renderer;

// Scene stages whose features draw into their own view.
enum class CompositorStage : std::uint8_t {
    PreOpaque,       // Before the view's opaque pass.
    Opaque,          // After the view's opaque pass, same view target.
    PostOpaque,      // After the opaque pass, before the transparent pass.
    PostTransparent, // After the transparent pass.
};

// Callback type of each scene stage.
template <CompositorStage S>
using StageCallback =
    std::conditional_t<S == CompositorStage::PreOpaque, ProjectPreOpaqueCallback, ProjectScenePassCallback>;

template <class Callback>
struct StageEntry {
    std::string name;
    std::int32_t priority = 0;
    bool enabledOverride = true;
    ViewScopeEnable scopes;
    Callback callback;
    RenderFeatureOwner owner = RenderFeatureOwner::Project;
};
using PreOpaqueEntry = StageEntry<ProjectPreOpaqueCallback>;
using SceneStageEntry = StageEntry<ProjectScenePassCallback>;

// Owns the PostComposite/PreUI blend chain and the PreOpaque/Opaque/PostOpaque/PostTransparent lists.
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

    // Sets the enabled flag of `name` for one view scope; it combines with the override. The shared scope has no
    // flag. False when no feature has that name.
    bool SetFeatureViewEnabled(const std::string& name, rg::ViewScope scope, bool enabled);

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

    // Scene-stage lists: no GPU-state slot, no blend mode, no private target; they draw into their own view.
    // False (logged) when `name` exists in any stage, or when `callback` does not match `stage`.
    bool RegisterStageFeature(CompositorStage stage, const std::string& name, std::int32_t priority,
        ProjectPreOpaqueCallback callback, RenderFeatureOwner owner = RenderFeatureOwner::Project);
    bool RegisterStageFeature(CompositorStage stage, const std::string& name, std::int32_t priority,
        ProjectScenePassCallback callback, RenderFeatureOwner owner = RenderFeatureOwner::Project);

    // False (logged) when `name` is not registered in `stage`.
    bool UnregisterStageFeature(CompositorStage stage, const char* name);

    // Sorted by priority ascending; the stage provider skips entries whose override is off.
    template <CompositorStage S>
    const std::vector<StageEntry<StageCallback<S>>>& StageFeaturesInPriorityOrder() const noexcept;

private:
    // An on-screen feature (PostComposite or PreUI).
    struct Entry {
        GtePluginRenderFeatureDescriptor descriptor{};
        bool enabledOverride = true;
        ViewScopeEnable scopes;
        ProjectRenderFeatureCallback projectCallback;
        int projectFeatureSlot = -1;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
