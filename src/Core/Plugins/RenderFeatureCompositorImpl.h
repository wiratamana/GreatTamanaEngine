#pragma once

// State and bodies behind RenderFeatureCompositor; only the compositor's own .cpp files include it.

#include "RenderFeatureCompositor.h"
#include "RenderFeatureNamePool.h"
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

    std::vector<Entry> postComposite;                  // Sorted by priority ascending.
    std::vector<Entry> preUi;                          // Sorted by priority ascending.
    std::vector<PreOpaqueEntry> preOpaque;             // Sorted by priority ascending.
    std::vector<PostOpaqueEntry> postOpaque;           // Sorted by priority ascending.
    std::vector<PostTransparentEntry> postTransparent; // Sorted by priority ascending.
    RenderFeatureNamePool namePool;

    static constexpr int kMaxConcurrentProjectRenderFeatures = 16;
    // Project features use a reusable slot index as their GPU-state key, so the key universe stays bounded.
    std::vector<int> freeProjectFeatureSlots;

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules);
    void ContributeRenderGraphPasses(const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out);
    std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;
    bool SetFeatureEnabled(const std::string& name, bool enabled);
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);
    bool RegisterFeature(
        const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback, RenderFeatureOwner owner);
    bool UnregisterProjectFeature(const char* name);
    bool RegisterPreOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner);
    bool UnregisterPreOpaqueFeature(const char* name);
    bool RegisterPostOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);
    bool UnregisterPostOpaqueFeature(const char* name);
    bool RegisterPostTransparentFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);
    bool UnregisterPostTransparentFeature(const char* name);

    // Searches postComposite, then preUi.
    Entry* FindEntryByName(const std::string& name);
    PreOpaqueEntry* FindPreOpaqueEntryByName(const std::string& name);
    PostOpaqueEntry* FindPostOpaqueEntryByName(const std::string& name);
    PostTransparentEntry* FindPostTransparentEntryByName(const std::string& name);

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
