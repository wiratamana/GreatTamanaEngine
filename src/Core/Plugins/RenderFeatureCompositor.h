#pragma once

#include "IPluginCapabilityOrchestrator.h"
#include "ProjectRenderFeatureCallback.h"
#include "ProjectPreOpaqueCallback.h"
#include "ProjectScenePassCallback.h"
#include "RenderFeatureDebugEntry.h"
#include "RenderFeatureNamePool.h"
#include "RenderFeatureDescriptor.h"

#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/ComputePipeline.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// The Project Assembly on-screen render-feature compositor: owns the
// PostComposite/PreUI blend chain (RenderFeatureBlend.comp, 5 blend modes
// selected per-feature via GtePluginRenderFeatureDescriptor::blendMode) and
// the PreOpaque/PostOpaque/PostTransparent registration lists.
namespace gte {

// Blend pipeline construction is lazily built once by
// EnsureBlendPipelineInitialized() and shared by every feature's dispatch.
struct RenderFeatureBlendPushConstants {
    float blendModeAndPad[4] = {}; // .x = RenderFeatureBlendMode, as a float cast to int in-shader
};

class Core;     // Forward declaration only - avoids a circular include with Core.h.
class Renderer; // Forward declaration only - held as a plain reference member.

class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    RenderFeatureCompositor(Core& core, Renderer& renderer);

    // Intentionally empty - IPluginCapabilityOrchestrator requires an
    // override but nothing calls this anymore.
    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // Read-only snapshot of the real, resolved ordering decision - walks
    // m_postComposite then m_preUi, the same order ContributeRenderGraphPasses()
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
    // Returns false (logged, never crashes) if the name already exists in
    // m_postComposite/m_preUi, if descriptor.stage is not wired in this
    // engine build, or if every project-feature GPU-state slot is claimed.
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
    // claimed GPU-state slot back to the free list so a LATER registration
    // (this project's, a renamed replacement, or a different project's) can
    // reuse it. Returns false if no entry with that name exists, or if the
    // found entry is NOT a project feature (its `projectCallback` is unset) -
    // a harmless, logged no-op either way.
    bool UnregisterProjectFeature(const char* name);

    // A Project Assembly's own PreOpaque feature. Not a reuse of Entry -
    // PreOpaque has no GPU-state slot, no blend mode, no private target:
    // it draws into its own render view and publishes directly. Public so
    // the PreOpaqueFeatures provider can name this type while iterating
    // PreOpaqueFeaturesInPriorityOrder()'s result.
    struct PreOpaqueEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectPreOpaqueCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // PreOpaque sibling of RegisterProjectFeature(). Name is assumed
    // already length-validated by the caller. Returns false (logged) if
    // `name` is already registered as PreOpaque, PostOpaque, PostTransparent,
    // PostComposite, or PreUI - all feature names share one global namespace.
    // Owner-fixed wrapper (Project) over RegisterPreOpaqueFeatureInternal() -
    // see RegisterBuiltInPreOpaqueFeature() immediately below for the
    // Engine-owned sibling, same "thin owner-fixed wrapper" shape as
    // RegisterFeature()/RegisterProjectFeature()/RegisterBuiltInFeature().
    bool RegisterPreOpaqueFeature(const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback)
    {
        return RegisterPreOpaqueFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Project);
    }

    // Engine-owned sibling of RegisterPreOpaqueFeature() immediately above -
    // same preconditions/refusal rules, marks the registered entry
    // RenderFeatureOwner::Engine so DebugSnapshot() reports isProjectFeature
    // == false for it (Core::AddBuiltInPreOpaquePass() is the only intended
    // caller - never recorded in ProjectAssemblyRegistrationLedger).
    bool RegisterBuiltInPreOpaqueFeature(
        const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback)
    {
        return RegisterPreOpaqueFeatureInternal(name, priority, std::move(callback), RenderFeatureOwner::Engine);
    }

    // Removes a previously-registered PreOpaque feature by name.
    // Returns false (logged, never crashes) if no entry with that name
    // exists.
    bool UnregisterPreOpaqueFeature(const char* name);

    // The new "PreOpaqueFeatures" RenderPipeline provider (Core.cpp,
    // PHASE3) walks this accessor, in order, skipping any entry whose
    // enabledOverride is currently false. Already sorted by priority
    // ascending (RegisterPreOpaqueFeature()/SetFeaturePriority() both
    // keep it that way via SortAndDetectCollisionsInPreOpaqueList()
    // below) - the provider itself never re-sorts.
    const std::vector<PreOpaqueEntry>& PreOpaqueFeaturesInPriorityOrder() const noexcept { return m_preOpaque; }

    // The PostOpaque sibling of PreOpaqueEntry above - a Project Assembly
    // callback guaranteed to run after the current view's own opaque pass
    // and before its own transparent pass. Shares ProjectScenePassCallback
    // with PostTransparentEntry below (one callback shape for both stages).
    struct PostOpaqueEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectScenePassCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // Owner-fixed wrappers over RegisterPostOpaqueFeatureInternal() - same
    // "thin owner-fixed wrapper" shape as RegisterPreOpaqueFeature()/
    // RegisterBuiltInPreOpaqueFeature() above.
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
    const std::vector<PostOpaqueEntry>& PostOpaqueFeaturesInPriorityOrder() const noexcept { return m_postOpaque; }

    // The PostTransparent sibling of PostOpaqueEntry above - guaranteed to
    // run after the current view's own transparent pass.
    struct PostTransparentEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectScenePassCallback callback;
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // Owner-fixed wrappers over RegisterPostTransparentFeatureInternal() -
    // same shape as above.
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
    const std::vector<PostTransparentEntry>& PostTransparentFeaturesInPriorityOrder() const noexcept
    {
        return m_postTransparent;
    }
private:
    // Real registration bodies - owner is explicit, never inferred. Public
    // RegisterXFeature()/RegisterBuiltInXFeature() above are thin,
    // owner-fixed wrappers; callers always use one of those, never these
    // directly.
    bool RegisterPreOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner);
    bool RegisterPostOpaqueFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);
    bool RegisterPostTransparentFeatureInternal(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner);

    struct Entry {
        GtePluginRenderFeatureDescriptor descriptor{};
        bool enabledOverride = true; // Host-side override; defaults true.

        // A Project Assembly's own on-screen render feature callback.
        // operator bool() == false when unused. projectFeatureSlot is
        // meaningful ONLY when projectCallback is set (bounded, reusable
        // GPU-state slot index, never keyed by descriptor.name).
        ProjectRenderFeatureCallback projectCallback;
        int projectFeatureSlot = -1;

        // Who registered this feature - Engine or Project. Explicit, never
        // inferred from projectCallback (every entry has one by construction).
        RenderFeatureOwner owner = RenderFeatureOwner::Project;
    };

    // Bundles exactly what one (feature, view) private-target slot needs -
    // mirrors AtmosphereLutRenderer's own AerialPerspectiveCompositeViewState
    // shape (a persistent output RenderTexture PLUS its own dedicated
    // ComputeDescriptorSet, never shared with any other slot - a Vulkan
    // descriptor set must never be Rewrite()-ed and dispatched against more
    // than once per frame for two DIFFERENT physical resources).
    struct PrivateTargetState {
        // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
        // `ComputeDescriptorSet opsDescriptorSet` removed - it was allocated
        // here unconditionally but only ever consumed by the now-deleted
        // DispatchOps() (`_v2` uber-shader path, ABI-only).
        std::optional<RenderTexture> texture; // created lazily, first use; resized in place on extent change.
    };

    // Bundles one blend-stage physical slot: either a per-(feature, view)
    // ACCUMULATOR (the output of feature i's blend, i < N-1, that becomes
    // feature i+1's own "currentInput"), or the per-VIEW SEED. Same
    // "persistent RenderTexture + its own dedicated ComputeDescriptorSet"
    // shape as PrivateTargetState, for the identical reason. The LAST entry
    // in a view's combined list never gets a texture here at all (its own
    // blend writes directly into the view's real composited handle instead
    // - see EnsureBlendStageDescriptorOnly()) - only a dedicated descriptor
    // set, since a blend dispatch always needs one regardless of where its
    // output physically lands.
    struct BlendStageState {
        ComputeDescriptorSet blendDescriptorSet;
        std::optional<RenderTexture> texture;
    };

    void EnsureTextureSized(std::optional<RenderTexture>& texture, const char* internedName, VkExtent2D extent);

    // Stable sort by priority, with a logged lexical tie-break on collision.
    // Shared by registration and SetFeaturePriority().
    static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);

    // Searches m_postComposite then m_preUi for a matching descriptor.name.
    // Returns nullptr if not found.
    Entry* FindEntryByName(const std::string& name);

    // True iff enabledOverride is true AND, only when `name` is also a
    // known toggle-registry entry, that registry's flag is true too. Never
    // creates a toggle-registry entry as a side effect of reading it.
    bool IsEffectivelyEnabled(const std::string& name, bool enabledOverride) const;

    PreOpaqueEntry* FindPreOpaqueEntryByName(const std::string& name);
    PostOpaqueEntry* FindPostOpaqueEntryByName(const std::string& name);
    PostTransparentEntry* FindPostTransparentEntryByName(const std::string& name);

    // Same stable-sort-plus-tie-break algorithm as SortAndDetectCollisionsInStage(),
    // against each smaller entry type.
    static void SortAndDetectCollisionsInPreOpaqueList(std::vector<PreOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostOpaqueList(std::vector<PostOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostTransparentList(std::vector<PostTransparentEntry>& entries);

    // Debug-build-only main-thread-affinity guard for RegisterFeature()/
    // UnregisterProjectFeature() - a true no-op in a release build.
    void AssertCalledFromMainThread() const;

    PrivateTargetState& EnsurePrivateTargetState(const char* internedName, VkExtent2D extent);
    BlendStageState& EnsureBlendStageDescriptorOnly(const char* internedName);
    BlendStageState& EnsureBlendStageState(const char* internedName, VkExtent2D extent);

    // Lazily builds m_blendPipeline/m_blendDescriptorSetLayout on first call.
    // Idempotent - safe to call from more than one call site per frame.
    void EnsureBlendPipelineInitialized();

    // The real blend/seed dispatch (RenderFeatureBlend.comp). `state` supplies
    // the descriptor set this dispatch rewrites/binds - its own `.texture`
    // field is irrelevant here since the destination handle is passed
    // explicitly. `blendMode` selects the blend formula via push constant;
    // `groupHeading` picks this dispatch's Render Graph panel sidebar
    // heading - opened as this method's own RenderFeatureScope internally.
    void DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn, VkSampler dstInSampler,
        rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination, BlendStageState& state,
        const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode, std::string_view groupHeading);

    Core& m_core;
    Renderer& m_renderer;
    VkDevice m_device = VK_NULL_HANDLE;

    // Captured at construction (always the engine's own main thread) -
    // compared against by AssertCalledFromMainThread().
    const std::thread::id m_mainThreadId = std::this_thread::get_id();

    // Lazily built by EnsureBlendPipelineInitialized() on first use.
    std::optional<ComputePipeline> m_blendPipeline; // RenderFeatureBlend.comp.
    VkDescriptorSetLayout m_blendDescriptorSetLayout = VK_NULL_HANDLE;

    std::vector<Entry> m_postComposite; // sorted by priority ascending
    std::vector<Entry> m_preUi;         // sorted by priority ascending
    std::vector<PreOpaqueEntry> m_preOpaque; // sorted by priority ascending - see PreOpaqueEntry's own doc comment.
    std::vector<PostOpaqueEntry> m_postOpaque;             // sorted by priority ascending
    std::vector<PostTransparentEntry> m_postTransparent;   // sorted by priority ascending
    RenderFeatureNamePool m_namePool;

    // Keyed by RenderFeatureNamePool's own interned "<Name>_<View>_Private" names.
    std::unordered_map<std::string, PrivateTargetState> m_privateTargetStates;
    // Keyed by RenderFeatureNamePool's own interned "<Name>_<View>_Accum" names
    // AND "RenderFeatureCompositor_<View>_Seed" names (same map, both roles -
    // they never collide, since interned names always carry either "_Accum"
    // or "_Seed" as an unambiguous suffix).
    std::unordered_map<std::string, BlendStageState> m_blendStageStates;

    // Generously sized against realistic usage - comfortably far below the
    // shared compute-descriptor-set pool's fixed capacity.
    static constexpr int kMaxConcurrentProjectRenderFeatures = 16;

    // Never keyed by descriptor.name - keyed by a small, fixed, reusable
    // slot index instead. RegisterProjectFeature() pops one off,
    // UnregisterProjectFeature() pushes it back.
    std::vector<int> m_freeProjectFeatureSlots;
};

} // namespace gte
