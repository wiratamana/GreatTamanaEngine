#pragma once

// editor-core-separation-9 campaign, PHASE2
// (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.4) - the gte_core-side
// implementation of IPluginRenderPassBuilder_v3 - constructed FRESH per
// plugin, per currently-visible view, per frame, by
// RenderFeatureCompositor::ContributeRenderGraphPasses() - never held past
// the end of that method's own per-entry loop iteration (mirrors
// PluginRenderPassBuilderAdapter_v2's own exact lifetime rule exactly).
//
// See task_manager/editor-core-separation-9/PHASE0_MASTER_STRATEGY.md Step
// 2.3 (both Corrections)/Step 2.5 (handle translation)/Step 2.6 (compositor
// wiring) and PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.4 (RenderPassEvent
// scheduling hazard)/Step 2.5 (imported-texture-sampler hazard)/Step 2.6 (a
// real, load-bearing Vulkan descriptor-set-pool-lifetime hazard) for the full
// correctness reasoning behind every non-obvious design choice in this file -
// this is NOT decorative background reading, every one of those sections
// documents a real bug this file's own shape exists to prevent.
//
// A FOURTH, real hazard found and fixed DURING this phase's own
// implementation (not in either master doc, since neither's own pseudocode
// caught it): this whole frame's passes across BOTH views are fully DECLARED
// (every `AddRenderGraphPasses()` call, for every loaded plugin, every view)
// BEFORE the single, shared `rg::RenderGraphBuilder` is compiled and
// EXECUTED exactly once (Core::BuildFrame()'s own
// `m_offscreenRenderPipeline.DeclareInto()` -> `builder.Finish()` ->
// `RenderGraphCompiler::Compile()` -> ONE `RenderGraph::ExecuteCompiledGraph()`
// call) - meaning THIS ADAPTER INSTANCE is already destroyed (it is a
// stack-local inside RenderFeatureCompositor::ContributeRenderGraphPasses()'s
// own per-entry loop iteration) by the time ANY pass's `execute` callback
// actually runs. A naive `execute` lambda capturing `this` (this adapter)
// would therefore be a REAL use-after-free the very first time this engine
// executes a frame with a `_v3` plugin loaded - not a theoretical concern.
// The FIX: the handle-translation table (`TranslationState` below) is
// heap-allocated via `std::shared_ptr`, constructed once per adapter
// instance and captured BY VALUE (extending its lifetime) into every one of
// this adapter's own passes' `execute` closures - `CommandRecorderAdapter`
// (below) holds this shared_ptr, plain references to the two LONG-LIVED,
// Core-owned singletons it also needs (`PluginRenderOperationRegistry&`/
// `RenderFeatureCompositor&`), and a BY-VALUE COPY of
// `m_opDescriptorSetKeyPrefix` - NEVER a reference/pointer back to this
// short-lived adapter instance itself. The SETUP callback (`IPluginPassSetupContext`,
// via `SetupContextAdapter`) safely keeps referencing this adapter directly,
// since `RenderGraphBuilder::AddPass()`'s own `setup` callback runs
// SYNCHRONOUSLY, before `AddRenderPass()` itself even returns - well before
// this adapter could possibly go out of scope.
//
// Handle translation (PHASE0_MASTER_STRATEGY.md Step 2.5): a
// PluginTextureHandle/PluginBufferHandle's own `index` is simply this
// adapter INSTANCE's own translation table index - NEVER numerically
// identical to (or interchangeable with) a real rg::TextureHandle/
// rg::BufferHandle. `generation` is a small, per-adapter-instance,
// process-wide monotonic counter (never reset, never per-frame, never
// per-handle) - two adapters built in two different frames (or for two
// different plugins the same frame) never share a generation value, which
// is what makes ResolveTranslatedTextureIn()/ResolveBufferIn()'s own "reject
// a stale/foreign handle" check below actually catch something.

#include "PluginRenderOperationRegistry.h"

#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class RenderFeatureCompositor;

class PluginRenderPassBuilderAdapter_v3 final : public IPluginRenderPassBuilder_v3 {
public:
    PluginRenderPassBuilderAdapter_v3(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget,
        PluginRenderOperationRegistry& operationRegistry, IPluginBlackboard& blackboard,
        rg::TextureHandle sceneColorTarget, VkSampler sceneColorSampler, RenderFeatureCompositor& compositor,
        std::string opDescriptorSetKeyPrefix);

    // --- IPluginRenderPassBuilder_v3 -----------------------------------------
    PluginTextureHandle CreateTexture(const char* debugName, const PluginTextureDesc& desc) override;
    PluginBufferHandle CreateBuffer(const char* debugName, const PluginBufferDesc& desc) override;
    bool TryGetNamedTexture(const char* semanticName, PluginTextureHandle& outHandle) override;
    PluginTextureHandle GetPrivateOutputTarget() override;
    void AddGraphicsPass(const char* debugName, SetupFn setup, ExecuteFn execute, void* userData) override;
    void AddComputePass(const char* debugName, SetupFn setup, ExecuteFn execute, void* userData) override;
    IPluginBlackboard& Blackboard() override;

private:
    // PHASE0_MASTER_STRATEGY.md Step 2.5 - one translated texture-table
    // entry. `externalSamplerOverride` is non-VK_NULL_HANDLE ONLY for the
    // cached "SceneColor" entry (see TryGetNamedTexture() below) -
    // Dispatch()/DrawFullscreenTriangle() prefer this over
    // ctx.resolveTexture(handle).sampler whenever it is set, since an
    // IMPORTED handle's own ctx-resolved sampler is always VK_NULL_HANDLE
    // (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.5).
    // `isPrivateOutputTarget` is true ONLY for the cached
    // GetPrivateOutputTarget() entry - refused as a CombinedImageSampler bind
    // target (no external sampler is tracked for a plugin's own in-progress
    // private output).
    struct TranslatedTexture {
        rg::TextureHandle handle;
        VkSampler externalSamplerOverride = VK_NULL_HANDLE;
        bool isPrivateOutputTarget = false;
    };

    // See this file's own top-of-file "FOURTH hazard" doc comment above for
    // why this exists as a SEPARATE, heap-allocated, shared_ptr-held struct
    // rather than plain adapter members - it must outlive this short-lived
    // adapter instance, since a plugin's own `execute` callback (captured
    // into a real rg::PassRecord) runs much later, well after this adapter
    // is destroyed.
    struct TranslationState {
        std::vector<TranslatedTexture> textures;
        std::vector<rg::BufferHandle> buffers;
        std::uint32_t generation = 0;
    };

    // The curated equivalent of rg::RenderGraphBuilder::PassBuilder, handed
    // to a plugin's own SetupFn - see IPluginPassSetupContext's own doc
    // comment (IPluginRenderPassBuilder_v3.h). A private nested class of the
    // owning adapter - per C++'s own "a nested class is a member and has the
    // same access rights as any other member" rule, its methods may freely
    // call the owning adapter's private static ResolveTranslatedTextureIn()/
    // ResolveBufferIn() helpers. SAFE to reference the owning adapter
    // directly (`m_owner`) - this class is only ever used SYNCHRONOUSLY,
    // during RenderGraphBuilder::AddPass()'s own `setup` callback, which
    // runs before AddRenderPass() itself even returns (see this file's own
    // top-of-file doc comment).
    class SetupContextAdapter final : public IPluginPassSetupContext {
    public:
        SetupContextAdapter(PluginRenderPassBuilderAdapter_v3& owner, rg::RenderGraphBuilder::PassBuilder& pass) noexcept
            : m_owner(owner)
            , m_pass(pass)
        {
        }

        void ReadTexture(PluginTextureHandle handle, PluginResourceAccess access) override;
        void WriteTexture(PluginTextureHandle handle, PluginResourceAccess access) override;
        void ReadBuffer(PluginBufferHandle handle, PluginResourceAccess access) override;
        void WriteBuffer(PluginBufferHandle handle, PluginResourceAccess access) override;
        void WriteColorAttachment(
            PluginTextureHandle handle, bool hasClearColor, float r, float g, float b, float a) override;

    private:
        PluginRenderPassBuilderAdapter_v3& m_owner;
        rg::RenderGraphBuilder::PassBuilder& m_pass;
    };

    // The curated, opaque-to-the-plugin recording surface handed to a
    // plugin's own ExecuteFn - see IPluginCommandRecorder's own doc comment
    // (IPluginRenderPassBuilder_v3.h). Constructed FRESH, as a stack-local,
    // inside a SINGLE `execute` closure invocation (RenderGraph::Execute()) -
    // deliberately holds NO reference/pointer back to the owning
    // PluginRenderPassBuilderAdapter_v3 (which is already destroyed by this
    // point - see this file's own top-of-file doc comment) - only a
    // `shared_ptr` to the still-alive `TranslationState`, plain references
    // to the two LONG-LIVED, Core-owned singletons it needs, and a
    // by-value-copied key-prefix string. `debugName` is THIS PASS'S OWN
    // literal debugName - needed to build the persistent descriptor-set
    // cache key `opDescriptorSetKeyPrefix + debugName`
    // (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.6).
    class CommandRecorderAdapter final : public IPluginCommandRecorder {
    public:
        CommandRecorderAdapter(std::shared_ptr<TranslationState> state, PluginRenderOperationRegistry& operationRegistry,
            RenderFeatureCompositor& compositor, std::string opDescriptorSetKeyPrefix, rg::PassContext& ctx,
            const char* debugName)
            : m_state(std::move(state))
            , m_operationRegistry(operationRegistry)
            , m_compositor(compositor)
            , m_opDescriptorSetKeyPrefix(std::move(opDescriptorSetKeyPrefix))
            , m_ctx(ctx)
            , m_debugName(debugName)
        {
        }

        void BindTexture(std::uint32_t slot, PluginTextureHandle handle) override;
        void BindBuffer(std::uint32_t slot, PluginBufferHandle handle) override;
        bool Dispatch(const char* opId, const void* paramBytes, std::size_t paramSize, std::uint32_t groupsX,
            std::uint32_t groupsY, std::uint32_t groupsZ) override;
        bool DrawFullscreenTriangle(const char* opId, const void* paramBytes, std::size_t paramSize) override;

    private:
        static constexpr std::size_t kMaxSlots = 8;

        struct SlotBinding {
            bool hasValue = false;
            bool isBuffer = false;
            PluginTextureHandle texture;
            PluginBufferHandle buffer;
        };

        // Shared implementation for Dispatch()/DrawFullscreenTriangle() -
        // steps 1 (unknown opId)/2 (wrong kind)/3 (exact paramSize match)/5
        // (per-slot binding validation) are IDENTICAL for both methods; only
        // step 4 (group-count cap, Dispatch()-only) and the final real
        // Vulkan recording call differ. Returns nullptr (already logged) on
        // any validation failure.
        const PluginRenderOpInfo* ValidateCommon(
            const char* opId, PluginRenderOpKind expectedKind, std::size_t paramSize) const;

        // Resolves every bound slot (already validated by ValidateCommon())
        // into a real ComputeDescriptorWrite vector, and returns the
        // persistent, never-recreated-per-frame descriptor set to bind -
        // shared by Dispatch()/DrawFullscreenTriangle() step 6.
        VkDescriptorSet BuildAndRewriteDescriptorSet(const PluginRenderOpInfo& op) const;

        std::shared_ptr<TranslationState> m_state;
        PluginRenderOperationRegistry& m_operationRegistry;
        RenderFeatureCompositor& m_compositor;
        std::string m_opDescriptorSetKeyPrefix;
        rg::PassContext& m_ctx;
        const char* m_debugName;
        SlotBinding m_slotBindings[kMaxSlots];
    };

    // Shared, pure-lookup helpers used by BOTH nested classes above (private
    // static members of the enclosing class are reachable from either nested
    // class, per C++'s own nested-class access rule) - operate on an
    // explicitly-passed `const TranslationState&` rather than `this`, so
    // BOTH the synchronous (SetupContextAdapter, via the owning adapter's
    // OWN `*m_state`) and the deferred (CommandRecorderAdapter, via its own
    // captured shared_ptr) call sites share the exact same validation logic
    // with zero duplication.
    static const TranslatedTexture* ResolveTranslatedTextureIn(
        const TranslationState& state, PluginTextureHandle handle) noexcept;
    static const rg::BufferHandle* ResolveBufferIn(const TranslationState& state, PluginBufferHandle handle) noexcept;

    const TranslatedTexture* ResolveTranslatedTexture(PluginTextureHandle handle) const noexcept
    {
        return ResolveTranslatedTextureIn(*m_state, handle);
    }
    const rg::BufferHandle* ResolveBuffer(PluginBufferHandle handle) const noexcept
    {
        return ResolveBufferIn(*m_state, handle);
    }

    rg::RenderGraphBuilder& m_builder;
    rg::TextureHandle m_privateTarget;
    PluginRenderOperationRegistry& m_operationRegistry;
    IPluginBlackboard& m_blackboard;
    rg::TextureHandle m_sceneColorTarget;
    VkSampler m_sceneColorSampler;
    RenderFeatureCompositor& m_compositor;
    std::string m_opDescriptorSetKeyPrefix;

    // Heap-allocated (see this file's own top-of-file "FOURTH hazard" doc
    // comment) - constructed ONCE, in this adapter's own constructor, never
    // reset/reassigned for this adapter instance's whole lifetime.
    std::shared_ptr<TranslationState> m_state;

    // Cached indices for GetPrivateOutputTarget()/TryGetNamedTexture("SceneColor")
    // - repeated calls within one AddRenderGraphPasses() invocation return the
    // IDENTICAL PluginTextureHandle every time (never mint a second entry for
    // the same underlying handle). Only ever read/written during the
    // synchronous declare phase - safe as plain adapter members (never
    // needed by a deferred `execute` closure).
    bool m_privateOutputTargetCached = false;
    PluginTextureHandle m_cachedPrivateOutputTarget;
    bool m_sceneColorCached = false;
    PluginTextureHandle m_cachedSceneColor;

    // Locked Architecture Decision #12 (PHASE0_MASTER_STRATEGY.md) - the
    // 32-CreateTexture()/CreateBuffer()-calls-per-invocation cap, enforced
    // across BOTH CreateTexture() and CreateBuffer() combined (a single
    // shared counter - the cap document says "at most 32
    // CreateTexture()/CreateBuffer() calls", not 32 of each). Declare-phase-
    // only, same reasoning as the cached handles above.
    std::uint32_t m_resourceCreationCallCount = 0;
};

} // namespace gte
