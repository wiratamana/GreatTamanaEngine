#pragma once

// Renderer-internal only - never included by a Project Assembly. Holds
// every Vk*/RenderTexture/ComputePipeline field and private helper
// RenderFeatureCompositor.h used to expose directly. Only
// RenderFeatureCompositor.cpp includes this.

#include "../../Core/Plugins/RenderFeatureCompositor.h"
#include "../../Core/Plugins/RenderFeatureNamePool.h"
#include "../Compute/ComputeDescriptorSet.h"
#include "../Compute/ComputePipeline.h"
#include "../GraphicsHandleRegistry.h"
#include "../RenderTexture.h"
#include "../RenderGraph/RenderGraphBuilder.h"
#include "../RenderGraph/RenderPipeline.h"

#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gte {

class Core;
class Renderer;

struct RenderFeatureCompositor::Impl {
    Impl(Core& core, Renderer& renderer);

    Core& core;
    Renderer& renderer;
    const std::thread::id mainThreadId = std::this_thread::get_id();

    // Lazily built by EnsureBlendPipelineInitialized() on first use.
    std::optional<ComputePipeline> blendPipeline; // RenderFeatureBlend.comp.
    VkDescriptorSetLayout blendDescriptorSetLayout = VK_NULL_HANDLE;

    // Bundles exactly what one (feature, view) private-target slot needs.
    struct PrivateTargetState {
        std::optional<RenderTexture> texture; // created lazily, resized in place on extent change.
    };

    // Bundles one blend-stage physical slot: either a per-(feature, view)
    // accumulator, or the per-view seed.
    struct BlendStageState {
        ComputeDescriptorSet blendDescriptorSet;
        std::optional<RenderTexture> texture;
    };

    // Cached per-(feature, view) sampler/image-view handle trio handed to a
    // project feature's ScenePassReadHandles - see GraphicsHandleRegistry.h's
    // CachedSamplerHandle/CachedImageViewHandle doc comments. Keeps the
    // opaque handle registries flat across frames instead of minting a
    // fresh, never-freed handle every single frame, per feature, per view.
    struct ScenePassHandleCache {
        CachedSamplerHandle colorSampler;
        CachedImageViewHandle depthImageView;
        CachedSamplerHandle depthSampler;
    };

    std::vector<Entry> postComposite; // sorted by priority ascending
    std::vector<Entry> preUi;         // sorted by priority ascending
    std::vector<PreOpaqueEntry> preOpaque;             // sorted by priority ascending
    std::vector<PostOpaqueEntry> postOpaque;           // sorted by priority ascending
    std::vector<PostTransparentEntry> postTransparent; // sorted by priority ascending
    RenderFeatureNamePool namePool;

    // Generously sized against realistic usage.
    static constexpr int kMaxConcurrentProjectRenderFeatures = 16;
    // Never keyed by descriptor.name - keyed by a small, fixed, reusable
    // slot index instead.
    std::vector<int> freeProjectFeatureSlots;

    // --- Mirrors of the public RenderFeatureCompositor API - the actual bodies. ---
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

    // Searches postComposite then preUi for a matching descriptor.name.
    Entry* FindEntryByName(const std::string& name);
    PreOpaqueEntry* FindPreOpaqueEntryByName(const std::string& name);
    PostOpaqueEntry* FindPostOpaqueEntryByName(const std::string& name);
    PostTransparentEntry* FindPostTransparentEntryByName(const std::string& name);

    // True iff enabledOverride is true AND, only when `name` is also a
    // known toggle-registry entry, that registry's flag is true too.
    bool IsEffectivelyEnabled(const std::string& name, bool enabledOverride) const;

    // Debug-build-only main-thread-affinity guard - a true no-op in release.
    void AssertCalledFromMainThread() const;

    void EnsureTextureSized(std::optional<RenderTexture>& texture, const char* internedName, VkExtent2D extent);
    PrivateTargetState& EnsurePrivateTargetState(const char* internedName, VkExtent2D extent);
    BlendStageState& EnsureBlendStageDescriptorOnly(const char* internedName);
    BlendStageState& EnsureBlendStageState(const char* internedName, VkExtent2D extent);

    // Lazily builds blendPipeline/blendDescriptorSetLayout on first call.
    void EnsureBlendPipelineInitialized();

    // The real blend/seed dispatch (RenderFeatureBlend.comp).
    void DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn, VkSampler dstInSampler,
        rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination, BlendStageState& state,
        const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode, std::string_view groupHeading);

    std::unordered_map<std::string, PrivateTargetState> privateTargetStates;
    std::unordered_map<std::string, BlendStageState> blendStageStates;
    std::unordered_map<std::string, ScenePassHandleCache> scenePassHandleCaches;
};

} // namespace gte
