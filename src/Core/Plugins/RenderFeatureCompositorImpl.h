#pragma once

// State and bodies behind RenderFeatureCompositor; only the compositor's own .cpp files include it.

#include "RenderFeatureCompositor.h"
#include "RenderFeatureNamePool.h"
#include "RenderFeatureStageList.h"
#include "../../Renderer/RenderGraph/PersistentTarget.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"
#include "../../Renderer/RenderGraph/Shader.h"
#include "../../Renderer/RenderGraph/StaticName.h"

#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gte {

class Core;

struct RenderFeatureCompositor::Impl {
    explicit Impl(Core& core);

    Core& core;
    const std::thread::id mainThreadId = std::this_thread::get_id();

    rg::Shader blendShader{ "RenderFeatureBlend", rg::ShaderKind::Compute };

    // One persistent target per interned name: a feature's private target, a blend accumulator or a view seed.
    std::unordered_map<std::string, rg::PersistentTarget> targets;

    std::vector<Entry> postComposite; // Sorted by priority ascending.
    std::vector<Entry> preUi;         // Sorted by priority ascending.

    // One list per scene stage; a new stage is one member, one CompositorStage value and one case in VisitStage().
    StageList<ProjectPreOpaqueCallback> preOpaque;
    StageList<ProjectScenePassCallback> opaque;
    StageList<ProjectScenePassCallback> postOpaque;
    StageList<ProjectScenePassCallback> postTransparent;
    RenderFeatureNamePool namePool;

    static constexpr int kMaxConcurrentProjectRenderFeatures = 16;
    // Project features use a reusable slot index as their GPU-state key, so the key universe stays bounded.
    std::vector<int> freeProjectFeatureSlots;

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules);
    void ContributeRenderGraphPasses(const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out);
    std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;
    bool SetFeatureEnabled(const std::string& name, bool enabled);
    bool SetFeatureViewEnabled(const std::string& name, rg::ViewScope scope, bool enabled);
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);
    bool RegisterFeature(
        const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback, RenderFeatureOwner owner);
    bool UnregisterProjectFeature(const char* name);
    bool RegisterStageFeature(CompositorStage stage, const std::string& name, std::int32_t priority,
        ProjectPreOpaqueCallback callback, RenderFeatureOwner owner);
    bool RegisterStageFeature(CompositorStage stage, const std::string& name, std::int32_t priority,
        ProjectScenePassCallback callback, RenderFeatureOwner owner);
    bool UnregisterStageFeature(CompositorStage stage, const char* name);

    // Calls fn(list, stageName) for the list of `stage`.
    template <class Fn>
    auto VisitStage(CompositorStage stage, Fn&& fn)
    {
        switch (stage) {
        case CompositorStage::PreOpaque:
            return fn(preOpaque, "PreOpaque");
        case CompositorStage::Opaque:
            return fn(opaque, "Opaque");
        case CompositorStage::PostOpaque:
            return fn(postOpaque, "PostOpaque");
        case CompositorStage::PostTransparent:
            break;
        }
        return fn(postTransparent, "PostTransparent");
    }

    // Calls fn(list, stageName) for every scene stage in order.
    template <class Fn>
    void ForEachStage(Fn&& fn)
    {
        fn(preOpaque, "PreOpaque");
        fn(opaque, "Opaque");
        fn(postOpaque, "PostOpaque");
        fn(postTransparent, "PostTransparent");
    }
    template <class Fn>
    void ForEachStage(Fn&& fn) const
    {
        fn(preOpaque, "PreOpaque");
        fn(opaque, "Opaque");
        fn(postOpaque, "PostOpaque");
        fn(postTransparent, "PostTransparent");
    }

    // Stage label of the feature called `name` in any list, or nullptr. Feature names share one namespace.
    const char* StageOfName(const std::string& name);
    bool IsNameTaken(const std::string& name) { return StageOfName(name) != nullptr; }

    // The on-screen list (postComposite or preUi) that holds `name`, or nullptr.
    std::vector<Entry>* EntryListOf(const std::string& name);
    Entry* FindEntryByName(const std::string& name);

    // The override flag AND, when `name` is a toggle-registry entry, that entry's flag.
    bool IsEffectivelyEnabled(const std::string& name, bool enabledOverride) const;

    // Debug-only guard: registration is main-thread-only.
    void AssertCalledFromMainThread() const;

    // Persistent color target for `internedName`; invalid when the cache refused it.
    rg::TextureHandle AcquireTarget(rg::RenderGraphBuilder& builder, const char* internedName, Extent2D extent);

    // One blend or seed dispatch; false when nothing was declared.
    bool DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn, rg::TextureHandle srcIn,
        rg::TextureHandle destination, rg::StaticName passName, rg::ViewScope scope, Extent2D extent,
        RenderFeatureBlendMode blendMode, std::string_view groupHeading);
};

} // namespace gte
