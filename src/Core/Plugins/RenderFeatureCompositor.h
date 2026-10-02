#pragma once

#include "IPluginCapabilityOrchestrator.h"
// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// #include "PluginRenderOperationRegistry.h" removed - that header/its
// backing .cpp are deleted outright this phase (ABI-only); the one piece
// this class needed from it (the RenderFeatureBlend.comp pipeline) was
// already pulled fully in-house by PHASE1 (see m_blendPipeline/
// m_blendDescriptorSetLayout below).
#include "ProjectRenderFeatureCallback.h"
#include "ProjectPreOpaqueCallback.h"
#include "ProjectScenePassCallback.h"
#include "RenderFeatureDebugEntry.h"
#include "RenderFeatureNamePool.h"
// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// used to be reached transitively through
// "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h" (deleted this
// phase alongside the rest of the ABI-only plugins/ tree) - now included
// directly from its new, gte_core-owned home (see that header's own
// top-of-file comment for the full relocation rationale).
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
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// editor-core-separation-6 campaign, PHASE4/PHASE5
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md,
// PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md) - the second real
// IPluginCapabilityOrchestrator implementation: originally the PERMANENT home
// for the `_v2`/`_v3` plugin render-feature pipeline (PHASE0_MASTER_STRATEGY.md's
// whole reason to exist at the time). better-render-pass-2 campaign, PHASE4
// (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) removed every piece of that
// ABI-plugin-discovery machinery outright (`Entry::moduleV2`/`moduleV3`, the
// `BlackboardAdapter`/`IPluginBlackboard` implementation, `OnPluginsLoaded()`'s
// own scan body - all dead code since nothing has been able to call
// `Core::LoadPlugins()` since PHASE2 of that same campaign) - this class is
// now PERMANENTLY the Project Assembly on-screen render-feature compositor
// only, via `RegisterProjectFeature()`/`UnregisterProjectFeature()` below. The
// real, host-owned GPU blend pipeline - never "last write wins on shared
// memory" - is otherwise completely unchanged.
//
// PHASE5 (editor-core-separation-6 campaign) replaced PHASE4's own temporary,
// Replace-equivalent blend stub (`RenderFeatureBlendStub.comp`/
// `m_blendStubPipeline`) with the real, permanent, 5-mode
// `RenderFeatureBlend.comp` uber blend shader (Replace / AlphaOver / Additive
// / Multiply / ScreenSpaceMask), selected per-feature via
// `GtePluginRenderFeatureDescriptor::blendMode` at dispatch time through a
// push constant - the ordering/private-target/collision-detection/seeding
// mechanism PHASE4 (of that campaign) built is completely unchanged; only
// WHAT the blend pass computes changed.
namespace gte {

// better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md) -
// `PluginRenderOperationRegistry` used to ALSO own the `RenderFeatureBlend.comp`
// blend pipeline/descriptor-set-layout (m_blendPipeline/m_blendDescriptorSetLayout)
// plus the `RenderFeatureBlendPushConstants` struct below - this class is their
// own ONLY real consumer (via DispatchBlend()), so that construction was pulled
// back OUT of the registry and fully into this class instead
// (EnsureBlendPipelineInitialized(), lazy-init-on-first-call, mirroring the
// registry's own prior idempotency discipline). `PluginRenderOperationRegistry`
// itself (the `_v2` uber-shader ops pipeline and the `_v3` string-keyed op
// registry - both genuinely ABI-only) was deleted outright by PHASE3 of that
// same campaign.
struct RenderFeatureBlendPushConstants {
    float blendModeAndPad[4] = {}; // .x = RenderFeatureBlendMode, as a float cast to int in-shader
};

class Core; // forward declaration only - this header must not #include "../Core.h"
            // (that would be a circular include: Core.h itself will gain a member
            // of type std::unique_ptr<IPluginCapabilityOrchestrator>, and
            // RenderFeatureCompositor.h is reachable from Core.cpp's own
            // #include list). The .cpp file #includes "../Core.h" for the real
            // Core& method calls (Core::FindPluginRenderFeatureTarget()).
class Renderer; // forward declaration only - held as a plain reference member;
                 // the .cpp file #includes "../Core.h", which transitively pulls
                 // in the real Renderer.h for every real method call.

class RenderFeatureCompositor final : public IPluginCapabilityOrchestrator {
public:
    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // trailing `PluginRenderOperationRegistry& operationRegistry` parameter
    // removed (that type is deleted outright this phase).
    RenderFeatureCompositor(Core& core, Renderer& renderer);

    // better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
    // intentionally empty override as of this campaign - see
    // RenderFeatureCompositor.cpp's own definition for the full "why"
    // (IPluginCapabilityOrchestrator still requires an override; nothing
    // calls this anymore).
    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - a read-only snapshot of this
    // compositor's own REAL, resolved ordering decision: walks m_postComposite
    // then m_preUi (the exact same combined order
    // ContributeRenderGraphPasses() itself uses), converting each Entry's
    // descriptor fields into small, human-readable strings (see the
    // RenderFeatureCompositor.cpp-local ToString(RenderFeatureStage)/
    // ToString(RenderFeatureBlendMode) helpers, mirroring RenderPassEvent's
    // own existing ToString() free-function precedent, RenderGraphTypes.h/
    // .cpp). Consumed by the Editor's "Render Graph" panel
    // (RenderGraphPanel::Build()) via Core::GetRenderFeatureCompositor()-
    // >DebugSnapshot(). Safe to call at most once per Editor frame - never
    // on a hot render path, and never mutates any of this class's own state.
    std::vector<RenderFeatureDebugEntry> DebugSnapshot() const;

    // editor-core-separation-8 campaign, PHASE2
    // (PHASE2_PLUGIN_RENDER_FEATURE_ENABLE_DISABLE_AND_PRIORITY.md) - host-side
    // enable/disable override. Finds `name` in EITHER m_postComposite OR
    // m_preUi and sets its enabledOverride. Returns false (no-op) if `name`
    // matches no registered feature - defense-in-depth; a caller (the panel,
    // or the HTTP bridge) is expected to only ever pass a name it already saw
    // via DebugSnapshot()/GET /render_graph's own render_features[] array.
    bool SetFeatureEnabled(const std::string& name, bool enabled);

    // editor-core-separation-8 campaign, PHASE2 - host-side LIVE priority
    // override (PHASE0_MASTER_STRATEGY.md's Locked Product Decision #2 -
    // safe, because ContributeRenderGraphPasses() rebuilds its entire
    // compositing chain fresh from m_postComposite/m_preUi every single
    // frame, and every interned target name is keyed by feature name + view
    // name, never by list position). Mutates the SAME host-side
    // Entry::descriptor.priority copy already made once at registration time,
    // then immediately re-sorts + re-runs the SAME collision-detection/tie-break
    // logic, for ONLY the one stage `name` belongs to. Returns false if `name`
    // matches no registered feature.
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);

    // editor-core-separation-9 campaign, PHASE2 (PHASE0_MASTER_STRATEGY.md
    // Step 2.6/Locked Architecture Decision #9) - a persistent (never
    // recreated per frame) descriptor-set cache, originally built for `_v3`
    // plugins' own Dispatch()/DrawFullscreenTriangle() calls. `key` must be
    // stable across frames for the same logical declaration - lazily
    // allocates (ONCE, ever, per distinct `key`) or returns the ALREADY-
    // existing `ComputeDescriptorSet` for `key`, exactly mirroring
    // `EnsurePrivateTargetState()`'s own "lazily created once, `Rewrite()`-only
    // thereafter, never recreated per frame" lifetime discipline. No current
    // real call site as of better-render-pass-2 (its one real caller, the
    // `_v3` plugin adapter, was deleted in that campaign's PHASE3) - kept as a
    // harmless, generically-useful mechanism, not itself ABI-dependent.
    ComputeDescriptorSet& EnsureV3OpDescriptorSet(const std::string& key, VkDescriptorSetLayout layout);

    // editor-core-separation-23 campaign, PHASE2
    // (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.2) - callable
    // incrementally, any time after this object exists - a Project Assembly
    // registers its own render feature(s) from its own GTE_RegisterProject
    // entry point. `descriptor` must already be fully built (via
    // MakeRenderFeatureDescriptor(), src/Core/Plugins/RenderFeatureDescriptor.h)
    // before this is called - this method never touches descriptor.name's own
    // length/validity itself (Core::RegisterProjectRenderFeature(), PHASE3,
    // does that BEFORE calling this). Returns false (GTE_LOG_WARNING, never
    // crashes) if descriptor.name already exists in either m_postComposite or
    // m_preUi (mirrors FindEntryByName()'s own linear-scan convention), if
    // descriptor.stage is not wired in this engine build, OR if every one of
    // the kMaxConcurrentProjectRenderFeatures slots is currently claimed by
    // some other still-registered project render feature.
    bool RegisterProjectFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback);

    // Removes a previously-registered project feature by name, releasing its
    // claimed GPU-state slot back to the free list so a LATER registration
    // (this project's, a renamed replacement, or a different project's) can
    // reuse it. Returns false if no entry with that name exists, or if the
    // found entry is NOT a project feature (its `projectCallback` is unset) -
    // a harmless, logged no-op either way.
    bool UnregisterProjectFeature(const char* name);

    // better-render-pass-5 effort, BLOCK 3, PHASE2 - a Project
    // Assembly's own PreOpaque feature. Deliberately NOT a reuse of
    // Entry above (PHASE0_MASTER_STRATEGY.md's Locked Design Decision
    // #5) - PreOpaque has no GPU-state slot, no blend mode, no private
    // render target, no "screen so far" to composite onto (nothing has
    // been drawn yet this point in the frame): it draws into its own,
    // self-managed render view (Core::CreateRenderView()) and
    // Publish()es its own result directly. Deliberately PUBLIC (not
    // nested inside the private section like Entry) so the new
    // "PreOpaqueFeatures" provider (Core.cpp, PHASE3 - a different
    // translation unit) can name this type directly when iterating
    // PreOpaqueFeaturesInPriorityOrder()'s result. `name` is a plain
    // std::string (not a fixed char[64] like
    // GtePluginRenderFeatureDescriptor::name) - there is no ABI
    // boundary reason to bound it here; Core::AddPreOpaquePass() still
    // enforces the SAME 63-byte reject-not-truncate discipline before
    // ever constructing one of these, purely for human-readability/log
    // consistency with every other registration surface in this
    // engine, not because this struct itself needs it.
    struct PreOpaqueEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true; // mirrors Entry::enabledOverride - same host-side override semantics.
        ProjectPreOpaqueCallback callback;
    };

    // The PreOpaque sibling of RegisterProjectFeature() above.
    // Deliberately a SEPARATE entry point (not a new RenderFeatureStage
    // case inside RegisterProjectFeature() itself) - see PreOpaqueEntry's
    // own doc comment above for why PreOpaque's storage shape is
    // genuinely different. `name` is assumed ALREADY LENGTH-VALIDATED by
    // the caller (Core::AddPreOpaquePass(), PHASE3, mirrors
    // Core::RegisterProjectRenderFeature()'s own identical "the Core
    // layer rejects an over-length debugName before calling into this
    // class at all" discipline) - this method never re-checks length
    // itself. Returns false (GTE_LOG_WARNING, never crashes) if `name`
    // is already registered as a PreOpaque feature, OR (PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #8 - confirmed by explicit user decision,
    // Step 2.5 item 3) if `name` already belongs to an existing
    // PostComposite/PreUI entry - PreOpaque feature names share ONE
    // GLOBAL namespace with PostComposite/PreUI, never their own
    // separate one, mirroring this engine's own existing precedent
    // (RegisterProjectFeature()'s own duplicate check already treats
    // m_postComposite/m_preUi as one shared namespace). Unlike
    // RegisterProjectFeature(), there is no slot pool to exhaust and no
    // unwired-stage case to refuse - PreOpaque IS the stage this method
    // exists for.
    bool RegisterPreOpaqueFeature(const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback);

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
    };

    bool RegisterPostOpaqueFeature(const std::string& name, std::int32_t priority, ProjectScenePassCallback callback);
    bool UnregisterPostOpaqueFeature(const char* name);
    const std::vector<PostOpaqueEntry>& PostOpaqueFeaturesInPriorityOrder() const noexcept { return m_postOpaque; }

    // The PostTransparent sibling of PostOpaqueEntry above - guaranteed to
    // run after the current view's own transparent pass.
    struct PostTransparentEntry {
        std::string name;
        std::int32_t priority = 0;
        bool enabledOverride = true;
        ProjectScenePassCallback callback;
    };

    bool RegisterPostTransparentFeature(
        const std::string& name, std::int32_t priority, ProjectScenePassCallback callback);
    bool UnregisterPostTransparentFeature(const char* name);
    const std::vector<PostTransparentEntry>& PostTransparentFeaturesInPriorityOrder() const noexcept
    {
        return m_postTransparent;
    }
private:
    struct Entry {
        GtePluginRenderFeatureDescriptor descriptor{};
        // editor-core-separation-8 campaign, PHASE2 - host-side-only
        // override. Defaults true - every existing registered feature
        // behaves EXACTLY as before until something explicitly calls
        // SetFeatureEnabled(false).
        bool enabledOverride = true;

        // editor-core-separation-23 campaign, PHASE1
        // (PHASE1_PROJECT_RENDER_FEATURE_CALLBACK_HEADER_AND_ENTRY_THIRD_KIND.md) -
        // a Project Assembly's own on-screen render feature: a plain
        // std::function-based callback. `operator bool() == false` when
        // unused. `projectFeatureSlot` is meaningful ONLY when
        // `projectCallback` is set - PHASE2 owns its assignment/meaning (the
        // bounded, reusable GPU-state slot index, never keyed by
        // descriptor.name - see PHASE0_MASTER_STRATEGY.md's Locked Decision
        // #4).
        ProjectRenderFeatureCallback projectCallback; // operator bool() == false when unused.
        int projectFeatureSlot = -1;                  // meaningful ONLY when projectCallback is set.
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

    // editor-core-separation-8 campaign, PHASE2 - extracted VERBATIM from the
    // former inline `sortAndDetectCollisions` lambda, so SetFeaturePriority()
    // can reuse the EXACT same sort+collision-tie-break behavior for a single
    // re-sort after a live priority change, without duplicating the logic.
    static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);

    // editor-core-separation-8 campaign, PHASE2 - shared lookup used by both
    // SetFeatureEnabled() and SetFeaturePriority(): searches m_postComposite
    // then m_preUi for an Entry whose descriptor.name matches `name` exactly.
    // Returns nullptr if not found. Non-const overload only (both callers
    // mutate through it).
    Entry* FindEntryByName(const std::string& name);

    // better-render-pass-5 effort, BLOCK 3, PHASE2 - PreOpaque sibling
    // of FindEntryByName() above, returning the genuinely different
    // PreOpaqueEntry* type (see that struct's own doc comment for why
    // this cannot be the SAME function). SetFeatureEnabled()/
    // SetFeaturePriority() each try FindEntryByName() first, then fall
    // back to this one.
    PreOpaqueEntry* FindPreOpaqueEntryByName(const std::string& name);

    // PostOpaque/PostTransparent siblings of FindPreOpaqueEntryByName() above.
    PostOpaqueEntry* FindPostOpaqueEntryByName(const std::string& name);
    PostTransparentEntry* FindPostTransparentEntryByName(const std::string& name);

    // PreOpaque sibling of SortAndDetectCollisionsInStage() above - the
    // SAME algorithm (stable sort by priority, same-priority collision
    // warning + lexical tie-break), against the new, smaller
    // PreOpaqueEntry type. Deliberately a separate function rather than
    // a template, matching this class's own existing style (every
    // other per-kind helper here is a plain, concrete function, never a
    // template).
    static void SortAndDetectCollisionsInPreOpaqueList(std::vector<PreOpaqueEntry>& entries);

    // PostOpaque/PostTransparent siblings of SortAndDetectCollisionsInPreOpaqueList() above.
    static void SortAndDetectCollisionsInPostOpaqueList(std::vector<PostOpaqueEntry>& entries);
    static void SortAndDetectCollisionsInPostTransparentList(std::vector<PostTransparentEntry>& entries);

    // editor-core-separation-23 campaign, PHASE5
    // (PHASE5_ORDERING_SAFETY_NET_AND_LIFETIME_CONFIRMATION.md, Step 3.2) - a
    // real, cheap, debug-build-only main-thread-affinity guard for
    // RegisterProjectFeature()/UnregisterProjectFeature() below (both are
    // documented, convention-only, main-thread-only contracts, exactly like
    // ContributeRenderGraphPasses() itself). Calls assert() directly -
    // already a true no-op in a release (NDEBUG) build.
    void AssertCalledFromMainThread() const;

    PrivateTargetState& EnsurePrivateTargetState(const char* internedName, VkExtent2D extent);
    BlendStageState& EnsureBlendStageDescriptorOnly(const char* internedName);
    BlendStageState& EnsureBlendStageState(const char* internedName, VkExtent2D extent);

    // better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md) -
    // lazily builds m_blendPipeline/m_blendDescriptorSetLayout on first call
    // (mirrors PluginRenderOperationRegistry::EnsureBuiltinsRegistered()'s own
    // prior idempotency discipline, moved here alongside the pipeline itself) -
    // idempotent, safe to call from more than one call site per frame. Called
    // from both EnsureBlendStageDescriptorOnly() (which needs
    // m_blendDescriptorSetLayout to allocate a ComputeDescriptorSet) and
    // DispatchBlend() itself (which needs m_blendPipeline) - calling it from
    // both guarantees correctness regardless of which is reached first, since
    // EnsureBlendStageDescriptorOnly() always runs before the DispatchBlend()
    // call that consumes its result at every real call site in this file.
    void EnsureBlendPipelineInitialized();

    // The real, permanent blend/seed dispatch (RenderFeatureBlend.comp,
    // PHASE5) - `state` supplies the dedicated descriptor set this dispatch
    // rewrites/binds; its own `.texture` field is irrelevant here (the
    // destination handle is passed explicitly, since the LAST entry in a
    // view's combined list writes into a handle `state` itself never owns).
    // `blendMode` selects the blend formula via the push constant -
    // RenderFeatureCompositor's own per-view "seed" dispatch always passes
    // RenderFeatureBlendMode::Replace explicitly, regardless of any
    // individual feature's own declared blend mode; every per-feature
    // dispatch passes that feature's own `descriptor.blendMode`.
    void DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn, VkSampler dstInSampler,
        rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination, BlendStageState& state,
        const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode);

    Core& m_core;
    Renderer& m_renderer;
    VkDevice m_device = VK_NULL_HANDLE;

    // editor-core-separation-23 campaign, PHASE5 - captured once, here, at
    // construction time (this object is always constructed on the engine's
    // own main thread, exactly like every other Core-owned orchestrator -
    // Core::RegisterBuiltinCapabilityOrchestrators(), called from Core's own
    // constructor). Compared against by AssertCalledFromMainThread() above.
    const std::thread::id m_mainThreadId = std::this_thread::get_id();

    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // `PluginRenderOperationRegistry& m_operationRegistry` removed, along with
    // the constructor parameter that supplied it - `PluginRenderOperationRegistry`
    // itself is deleted outright this phase (ABI-only: it backed the `_v2`
    // DispatchOps() uber-shader dispatch and the `_v3` adapter's own op
    // registry, both deleted alongside it). `m_device` is now resolved
    // directly from `m_renderer.GetVulkanContextInfo().device` instead (see
    // ContributeRenderGraphPasses(), RenderFeatureCompositor.cpp).

    // better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md) -
    // RELOCATED here from PluginRenderOperationRegistry (m_blendPipeline/
    // m_blendDescriptorSetLayout there) - this class is their own ONLY real
    // consumer (DispatchBlend()/EnsureBlendStageDescriptorOnly()), so pipeline
    // ownership was pulled fully in-house instead of reaching through the
    // registry's own accessors. Lazily built by
    // EnsureBlendPipelineInitialized() on first use.
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

    // editor-core-separation-9 campaign, PHASE2 - see EnsureV3OpDescriptorSet()'s
    // own doc comment above. Keyed by a plain std::string key - no
    // RenderFeatureNamePool interning needed here, since this map is never
    // consulted by anything that needs a stable const char*, unlike
    // PassRecord::name. PERSISTENT for the entire process lifetime - NEVER
    // cleared/recreated per frame.
    std::unordered_map<std::string, ComputeDescriptorSet> m_v3OpDescriptorSets;

    // editor-core-separation-23 campaign, PHASE2
    // (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.1) -
    // generously sized against realistic usage (mirrors GpuResourceFactory's
    // own "generously sized, a low hundreds not thousands" compute-pool
    // sizing philosophy) - 16 concurrently REGISTERED Project Assembly render
    // features (summed across every currently-loaded project) is far beyond
    // what any real session needs at once, while costing at most 16 (slots) x
    // 2 (views) x 2 (private + blend descriptor sets per slot) = 64 permanent
    // entries out of the shared pool's fixed 256 - comfortable headroom
    // alongside every other existing compute consumer (confirmed against
    // GpuResourceFactory.cpp's real, current pool sizing, kMaxComputeDescriptorSets
    // = 256, re-confirmed live during this phase).
    static constexpr int kMaxConcurrentProjectRenderFeatures = 16;

    // A Project Assembly's own render-feature GPU state is NEVER keyed by its
    // human-typed descriptor.name (PHASE0_MASTER_STRATEGY.md's Locked
    // Decision #4) - it is keyed by one of these small, fixed, reusable slot
    // indices instead. Initialized in the constructor to
    // {0, 1, ..., kMaxConcurrentProjectRenderFeatures - 1}; RegisterProjectFeature()
    // pops one off, UnregisterProjectFeature() pushes it back.
    std::vector<int> m_freeProjectFeatureSlots;
};

} // namespace gte
