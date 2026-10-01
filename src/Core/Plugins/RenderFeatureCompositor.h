#pragma once

#include "IPluginCapabilityOrchestrator.h"
// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// #include "PluginRenderOperationRegistry.h" removed - that header/its
// backing .cpp are deleted outright this phase (ABI-only); the one piece
// this class needed from it (the RenderFeatureBlend.comp pipeline) was
// already pulled fully in-house by PHASE1 (see m_blendPipeline/
// m_blendDescriptorSetLayout below).
#include "ProjectRenderFeatureCallback.h"
#include "RenderFeatureDebugEntry.h"
#include "RenderFeatureNamePool.h"

#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/ComputePipeline.h"
#include "../../Renderer/RenderTexture.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"
#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h"

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
// IPluginCapabilityOrchestrator implementation: the PERMANENT home for the
// `_v2` render-feature pipeline (PHASE0_MASTER_STRATEGY.md's whole reason to
// exist). Every loaded IRenderFeatureModule_v2 gets its OWN private
// offscreen render target, composited in explicit, author-declared
// stage+priority order, through a real, host-owned GPU blend pipeline -
// never "last write wins on shared memory" the way the legacy `_v1` path
// (LegacyRenderFeatureOrchestrator) still works, forever, by design (Locked
// Design Decision #9 - `_v1` and `_v2` are never unified).
//
// PHASE5 replaced PHASE4's own temporary, Replace-equivalent blend stub
// (`RenderFeatureBlendStub.comp`/`m_blendStubPipeline`) with the real,
// permanent, 5-mode `RenderFeatureBlend.comp` uber blend shader (Replace /
// AlphaOver / Additive / Multiply / ScreenSpaceMask), selected per-plugin via
// `GtePluginRenderFeatureDescriptor::blendMode` at dispatch time through a
// push constant - the ordering/private-target/collision-detection/seeding
// mechanism PHASE4 built is completely unchanged; only WHAT the blend pass
// computes changed.
//
// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md) - this class no longer OWNS
// the "uber ops"/blend `ComputePipeline`s/`VkDescriptorSetLayout`s directly -
// that ownership moved into the new, host-owned `PluginRenderOperationRegistry`
// (`m_operationRegistry` below, a reference to the ONE instance `Core` owns) -
// a pure refactor, zero `_v2` behavior change. This class ALSO gained a
// second, alternate per-entry module pointer (`Entry::moduleV3`) and a new,
// PERSISTENT (never-recreated-per-frame) descriptor-set cache
// (`m_v3OpDescriptorSets`/`EnsureV3OpDescriptorSet()`) for `_v3` plugins' own
// `Dispatch()`/`DrawFullscreenTriangle()` calls - see that method's own doc
// comment for the real, load-bearing Vulkan descriptor-set-pool-lifetime
// hazard this exists to close (PHASE0_MASTER_STRATEGY.md Step 2.6).
namespace gte {

// better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md) -
// `PluginRenderOperationRegistry` used to ALSO own the `RenderFeatureBlend.comp`
// blend pipeline/descriptor-set-layout (m_blendPipeline/m_blendDescriptorSetLayout)
// plus the `RenderFeatureBlendPushConstants` struct below - this class is their
// own ONLY real consumer (via DispatchBlend()), so that construction was pulled
// back OUT of the registry and fully into this class instead
// (EnsureBlendPipelineInitialized(), lazy-init-on-first-call, mirroring the
// registry's own prior idempotency discipline). The registry keeps owning the
// `_v2` uber-shader ops pipeline (m_opsPipeline/OpsPipeline()/
// OpsDescriptorSetLayout()) and the `_v3` string-keyed op registry - both
// genuinely ABI-only, unlike the blend pipeline, which is load-bearing for
// EVERY composited feature regardless of origin (plugin OR Project Assembly).
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

    void OnPluginsLoaded(const std::vector<IPluginModule*>& modules) override;
    void ContributeRenderGraphPasses(
        const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>& out) override;

    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - a read-only, `_v2`-ABI-free
    // snapshot of this compositor's own REAL, resolved ordering decision:
    // walks m_postComposite then m_preUi (the exact same combined order
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

    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // DispatchOps() (the `_v2` uber-shader dispatch, RenderFeatureOps.comp)
    // removed outright - its only real caller, PluginRenderPassBuilderAdapter_v2,
    // is deleted this same phase, and its own RenderFeatureOpsPushConstants
    // parameter type lived in the now-deleted PluginRenderOperationRegistry.h.

    // editor-core-separation-8 campaign, PHASE2
    // (PHASE2_PLUGIN_RENDER_FEATURE_ENABLE_DISABLE_AND_PRIORITY.md) - host-side
    // enable/disable override. Finds `name` in EITHER m_postComposite OR
    // m_preUi (a plugin's own descriptor.name is unique by construction - two
    // plugins sharing a name is not a case this engine defends against
    // anywhere else either) and sets its enabledOverride. Returns false
    // (no-op) if `name` matches no loaded _v2 plugin - defense-in-depth; a
    // caller (the panel, PHASE4, or the HTTP bridge, PHASE5) is expected to
    // only ever pass a name it already saw via DebugSnapshot()/
    // GET /render_graph's own render_features[] array.
    bool SetFeatureEnabled(const std::string& name, bool enabled);

    // editor-core-separation-8 campaign, PHASE2 - host-side LIVE priority
    // override (PHASE0_MASTER_STRATEGY.md's Locked Product Decision #2 -
    // safe, because ContributeRenderGraphPasses() rebuilds its entire
    // compositing chain fresh from m_postComposite/m_preUi every single
    // frame, and every interned target name is keyed by plugin name + view
    // name, never by list position). Mutates the SAME host-side
    // Entry::descriptor.priority copy already made once at OnPluginsLoaded()
    // time (never writes back into the plugin's own memory), then
    // immediately re-sorts + re-runs the SAME collision-detection/tie-break
    // logic OnPluginsLoaded() already uses, for ONLY the one stage `name`
    // belongs to. Returns false if `name` matches no loaded _v2 plugin.
    bool SetFeaturePriority(const std::string& name, std::int32_t priority);

    // editor-core-separation-9 campaign, PHASE2 (PHASE0_MASTER_STRATEGY.md
    // Step 2.6/Locked Architecture Decision #9) - the FIX for a real,
    // load-bearing Vulkan descriptor-set-pool-lifetime hazard found during
    // this campaign's own review pass: a naive `_v3` adapter-instance-scoped
    // descriptor-set cache would call `AllocateComputeDescriptorSet()` again
    // EVERY FRAME (a fresh PluginRenderPassBuilderAdapter_v3 is constructed
    // per plugin/per view/per frame, mirroring `_v2`'s own adapter lifetime),
    // permanently consuming one more of `GpuResourceFactory`'s fixed 256-set
    // compute descriptor pool slots each time - exhausting it within seconds
    // of real runtime. `key` must be stable across frames for the SAME
    // literal `AddGraphicsPass()`/`AddComputePass()` declaration (built by the
    // adapter as `"<Plugin>_<View>_" + debugName` - see
    // PluginRenderPassBuilderAdapter_v3::Dispatch()/DrawFullscreenTriangle())
    // - lazily allocates (ONCE, ever, per distinct `key`) or returns the
    // ALREADY-existing `ComputeDescriptorSet` for `key`, exactly mirroring
    // `EnsurePrivateTargetState()`'s own "lazily created once, `Rewrite()`-only
    // thereafter, never recreated per frame" lifetime discipline. Because the
    // cache key includes the pass's own literal `debugName`, a SINGLE plugin
    // dispatching the SAME `opId` twice in one frame from two DIFFERENT
    // `AddComputePass()`/`AddGraphicsPass()` declarations (two different
    // `debugName`s) gets two DIFFERENT, independent, persistent descriptor
    // sets - closing a second, independent same-frame descriptor-content
    // collision hazard alongside the pool-exhaustion one (see
    // PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.6 for the full
    // mechanical reasoning behind both). A plugin author calling `Dispatch()`
    // more than once inside the SAME pass's `execute` callback with different
    // bindings against the same `opId` is still sharing ONE descriptor set
    // within that one call - document this as a real, narrow constraint
    // ("call `Dispatch()` at most once per real bound-resource combination
    // per declared pass") - no real use case in this campaign's own PHASE2/
    // PHASE3 scope ever needs more than that.
    ComputeDescriptorSet& EnsureV3OpDescriptorSet(const std::string& key, VkDescriptorSetLayout layout);

    // editor-core-separation-23 campaign, PHASE2
    // (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.2) - callable
    // incrementally, any time after this object exists (unlike
    // OnPluginsLoaded()'s one-time bulk scan) - a Project Assembly registers
    // its own render feature(s) from its own GTE_RegisterProject entry point.
    // `descriptor` must already be fully built (via MakeRenderFeatureDescriptor(),
    // plugins/gte_plugin_abi/RenderFeatureDescriptor.h) before this is called -
    // this method never touches descriptor.name's own length/validity itself
    // (Core::RegisterProjectRenderFeature(), PHASE3, does that BEFORE calling
    // this). Returns false (GTE_LOG_WARNING, never crashes) if descriptor.name
    // already exists in either m_postComposite or m_preUi (mirrors
    // FindEntryByName()'s own linear-scan convention), if descriptor.stage is
    // not wired in this engine build (mirrors OnPluginsLoaded()'s own
    // refusal), OR if every one of the kMaxConcurrentProjectRenderFeatures
    // slots is currently claimed by some other still-registered project
    // render feature.
    bool RegisterProjectFeature(const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback);

    // Removes a previously-registered project feature by name, releasing its
    // claimed GPU-state slot back to the free list so a LATER registration
    // (this project's, a renamed replacement, or a different project's) can
    // reuse it. Returns false if no entry with that name exists, or if the
    // found entry is NOT a project feature (its `projectCallback` is unset) -
    // a harmless, logged no-op either way; never touches a
    // moduleV2/moduleV3 entry's slot bookkeeping (it never held one).
    bool UnregisterProjectFeature(const char* name);

private:
    // editor-core-separation-9 campaign, PHASE4
    // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.1) - the real
    // IPluginBlackboard implementation, replacing PHASE2's temporary
    // NoOpPluginBlackboard stand-in. A thin wrapper around this compositor's
    // own m_blackboard map (below) - held as ONE instance for this
    // compositor's entire lifetime (never re-constructed per frame/per
    // plugin), handed by reference to every _v3 adapter constructed this
    // frame, since every _v3 plugin declaring passes THIS frame must see the
    // SAME shared map contents (mirrors rg::RenderPassBlackboard's own
    // per-frame-shared-instance lifetime, RenderPipeline.h). The plugin ABI
    // (IPluginRenderPassBuilder_v3.h) has no logging capability of its own,
    // so Publish()/Fetch() log host-side, on this compositor's behalf - a
    // one-time-per-key GTE_LOG_INFO the first time a given key is ever
    // published/successfully fetched (never per-frame spam for the steady-
    // state success path), and a GTE_LOG_WARNING every time a Fetch() call
    // genuinely fails (key never published this frame, or published under a
    // different PluginBlackboardValueKind) - this is the ONLY externally
    // observable (GET /get_logs) confirmation available for a plugin's own
    // Publish()/Fetch() calls, since a plugin cannot log anything itself.
    class BlackboardAdapter final : public IPluginBlackboard {
    public:
        explicit BlackboardAdapter(RenderFeatureCompositor& owner) noexcept
            : m_owner(owner)
        {
        }

        void Publish(const char* key, const PluginBlackboardValue& value) override;
        bool Fetch(const char* key, PluginBlackboardValueKind expectedKind, PluginBlackboardValue& outValue) const override;

    private:
        RenderFeatureCompositor& m_owner;
    };

    struct Entry {
        // editor-core-separation-9 campaign, PHASE2 - RENAMED from `module`
        // (for symmetry with the new `moduleV3` below) - every pre-existing
        // reference updated in the same edit (OnPluginsLoaded()/
        // ContributeRenderGraphPasses()). A loaded plugin implements EITHER
        // `_v2` OR `_v3`, never both (OnPluginsLoaded() queries `_v3` FIRST,
        // since it is now the recommended path - PHASE0_MASTER_STRATEGY.md
        // Locked Product Decision #1) - so exactly one of moduleV2/moduleV3
        // is non-null for any real Entry.
        IRenderFeatureModule_v2* moduleV2 = nullptr;
        IRenderFeatureModule_v3* moduleV3 = nullptr;
        GtePluginRenderFeatureDescriptor descriptor{};
        // editor-core-separation-8 campaign, PHASE2 - host-side-only
        // override, NEVER part of the plugin's own descriptor/ABI (the
        // plugin ABI stays byte-for-byte unchanged - see
        // PHASE0_MASTER_STRATEGY.md's Step 2.2/Locked Product Decision,
        // mirroring GtePluginRenderFeatureDescriptor's own "the HOST NEVER
        // trusts a plugin to self-order at runtime" rule, extended here to
        // "and the host may now ALSO fully hide a plugin from the
        // compositing chain, without the plugin itself ever knowing").
        // Defaults true - every existing loaded plugin behaves EXACTLY as
        // before this phase until something explicitly calls
        // SetFeatureEnabled(false).
        bool enabledOverride = true;

        // editor-core-separation-23 campaign, PHASE1
        // (PHASE1_PROJECT_RENDER_FEATURE_CALLBACK_HEADER_AND_ENTRY_THIRD_KIND.md) -
        // a Project Assembly's own on-screen render feature: a plain
        // std::function-based callback, no ABI, no QueryCapability() - the
        // third, additive module-kind alongside moduleV2/moduleV3 above.
        // `operator bool() == false` when unused (a plugin-authored Entry
        // never sets this). `projectFeatureSlot` is meaningful ONLY when
        // `projectCallback` is set - PHASE2 owns its assignment/meaning (the
        // bounded, reusable GPU-state slot index, never keyed by
        // descriptor.name - see PHASE0_MASTER_STRATEGY.md's Locked Decision
        // #4).
        ProjectRenderFeatureCallback projectCallback; // operator bool() == false when unused.
        int projectFeatureSlot = -1;                  // meaningful ONLY when projectCallback is set.
    };

    // Bundles exactly what one (plugin, view) private-target slot needs -
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

    // Bundles one blend-stage physical slot: either a per-(plugin, view)
    // ACCUMULATOR (the output of plugin i's blend, i < N-1, that becomes
    // plugin i+1's own "currentInput"), or the per-VIEW SEED. Same
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

    // editor-core-separation-8 campaign, PHASE2 - extracted VERBATIM from
    // OnPluginsLoaded()'s own former inline `sortAndDetectCollisions` lambda,
    // so SetFeaturePriority() can reuse the EXACT same sort+collision-tie-
    // break behavior for a single re-sort after a live priority change,
    // without duplicating the logic.
    static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);

    // editor-core-separation-8 campaign, PHASE2 - shared lookup used by both
    // SetFeatureEnabled() and SetFeaturePriority(): searches m_postComposite
    // then m_preUi for an Entry whose descriptor.name matches `name` exactly.
    // Returns nullptr if not found. Non-const overload only (both callers
    // mutate through it).
    Entry* FindEntryByName(const std::string& name);

    // editor-core-separation-23 campaign, PHASE5
    // (PHASE5_ORDERING_SAFETY_NET_AND_LIFETIME_CONFIRMATION.md, Step 3.2) - a
    // real, cheap, debug-build-only main-thread-affinity guard for
    // RegisterProjectFeature()/UnregisterProjectFeature() below (both are
    // documented, convention-only, main-thread-only contracts, exactly like
    // ContributeRenderGraphPasses() itself - see this class's own header
    // comment history) - catches a future violation immediately and loudly
    // via assert(), instead of relying on convention/comments alone. No
    // reusable engine-wide thread-affinity helper exists anywhere in this
    // codebase (confirmed via a fresh, whole-src/ search_in_dir sweep before
    // writing this) and nothing else in this engine needs one yet, so this
    // stays the SMALLEST possible mechanism, scoped to this one class alone
    // (an ask_questions checkpoint this phase's own file called out
    // explicitly - resolved this way, left to implementer judgment). Calls
    // assert() directly - already a true no-op in a release (NDEBUG) build,
    // matching every other assert() call site in this codebase (none of them
    // are separately wrapped in an explicit #ifndef NDEBUG either), so no
    // extra preprocessor guard is needed to keep this debug-build-only.
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
    // individual plugin's own declared blend mode; every per-plugin dispatch
    // passes that plugin's own `descriptor.blendMode`.
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
    RenderFeatureNamePool m_namePool;

    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Private" names.
    std::unordered_map<std::string, PrivateTargetState> m_privateTargetStates;
    // Keyed by RenderFeatureNamePool's own interned "<Plugin>_<View>_Accum" names
    // AND "RenderFeatureCompositor_<View>_Seed" names (same map, both roles -
    // they never collide, since interned names always carry either "_Accum"
    // or "_Seed" as an unambiguous suffix).
    std::unordered_map<std::string, BlendStageState> m_blendStageStates;

    // editor-core-separation-9 campaign, PHASE2 - see EnsureV3OpDescriptorSet()'s
    // own doc comment above for the full, load-bearing reasoning. Keyed by
    // "<Plugin>_<View>_<PassDebugName>" (a plain std::string key - no
    // RenderFeatureNamePool interning needed here, since this map is never
    // consulted by anything that needs a stable const char*, unlike
    // PassRecord::name). ONE entry per literal AddGraphicsPass()/
    // AddComputePass() debugName a _v3 plugin ever declares, for its own
    // lifetime - bounded by (loaded _v3 plugin count) x (that plugin's own
    // pass count) x (2 views), exactly like m_privateTargetStates/
    // m_blendStageStates are already bounded. PERSISTENT for the entire
    // process lifetime - NEVER cleared/recreated per frame.
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

    // editor-core-separation-9 campaign, PHASE4 - the REAL, per-frame
    // cross-plugin blackboard storage (Locked Architecture Decision #13,
    // PHASE0_MASTER_STRATEGY.md) - cleared at the very START of
    // ContributeRenderGraphPasses() (mirrors rg::RenderPassBlackboard's own
    // per-frame lifetime exactly). Shared across BOTH _v2 and _v3 entries in
    // principle (the storage itself does not care which ABI version
    // published into it), though only _v3 exposes it to plugins this
    // campaign.
    std::unordered_map<std::string, PluginBlackboardValue> m_blackboard;

    // One-time-per-key diagnostic latches (see BlackboardAdapter's own doc
    // comment above) - deliberately NEVER cleared per frame (unlike
    // m_blackboard itself), so a key that is published/fetched successfully
    // every single frame logs exactly once for the whole process lifetime,
    // not once per frame.
    std::unordered_set<std::string> m_blackboardLoggedPublishKeys;
    std::unordered_set<std::string> m_blackboardLoggedFetchSuccessKeys;

    // The ONE BlackboardAdapter instance handed to every _v3 adapter
    // constructed this frame (ContributeRenderGraphPasses()) - constructed
    // once, in this compositor's own constructor, alongside every other
    // member.
    BlackboardAdapter m_blackboardAdapter;
};

} // namespace gte
