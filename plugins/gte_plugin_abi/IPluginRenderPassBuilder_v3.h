#pragma once

#include <cstddef>
#include <cstdint>

#include "PluginRenderResource.h"

// editor-core-separation-9 campaign, PHASE1
// (PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md) - the ADDITIVE, v3
// sibling of IPluginRenderPassBuilder_v2.h. _v1/_v2 are NEVER touched,
// deprecated, or removed by this campaign (PHASE0_MASTER_STRATEGY.md Locked
// Product Decision #1) - this is simply the RECOMMENDED path for new plugin
// authors going forward. Unlike _v2's closed enumeration of 3 fixed C++
// virtual methods (one per hardcoded effect), _v3 is a real, generic,
// two-phase setup/execute resource-graph builder - see
// PHASE0_MASTER_STRATEGY.md's Step 1/2 for the full "why". Every method on
// every type in this file uses ONLY plain built-in types, PluginTextureHandle/
// PluginBufferHandle, or a raw reference/pointer to another gte_plugin_abi
// interface type - never std::string/std::vector/std::function/std::any/
// std::optional, per PublicSurface.md's ABI-boundary rule. Nothing in this
// file is implemented or called yet this phase - PHASE2 (the adapter) and
// PHASE4 (the blackboard) are the first real consumers.

namespace gte {

// --- Setup-time pass builder -----------------------------------------------
//
// The curated equivalent of rg::RenderGraphBuilder::PassBuilder
// (RenderGraphBuilder.h) - deliberately the SAME small vocabulary
// (ReadTexture/WriteTexture/ReadBuffer/WriteBuffer/WriteColorAttachment),
// never a per-effect method (Design Doc R15). A pass's `setup` callback
// (see IPluginRenderPassBuilder_v3::SetupFn below) receives one of these -
// it may ONLY declare reads/writes here, never record GPU work (mirrors
// rg::RenderGraphBuilder::AddPass()'s own documented two-phase reasoning,
// RenderGraphBuilder.h ~line 394).
class IPluginPassSetupContext {
public:
    virtual ~IPluginPassSetupContext() = default;

    virtual void ReadTexture(PluginTextureHandle handle, PluginResourceAccess access) = 0;
    virtual void WriteTexture(PluginTextureHandle handle, PluginResourceAccess access) = 0;
    virtual void ReadBuffer(PluginBufferHandle handle, PluginResourceAccess access) = 0;
    virtual void WriteBuffer(PluginBufferHandle handle, PluginResourceAccess access) = 0;

    // hasClearColor == false preserves existing contents (LOAD_OP_LOAD),
    // exactly like rg::RenderGraphBuilder::PassBuilder::WriteColorAttachment()'s
    // own real behavior - no std::optional crosses the ABI (PublicSurface.md),
    // so the "no clear" case is spelled out as an explicit bool flag instead.
    virtual void WriteColorAttachment(PluginTextureHandle handle, bool hasClearColor, float r, float g, float b,
        float a) = 0;
};

// --- The registry-backed group-count cap (Locked Product Decision #2) -----
//
// PHASE0_MASTER_STRATEGY.md Locked Product Decision #2 - the hard,
// host-enforced cap on IPluginCommandRecorder::Dispatch()'s own
// groupsX/groupsY/groupsZ arguments (each independently <= this value). At
// the engine's standard 16x16 local-workgroup-size convention this is up to
// ~1,048,576 threads per single Dispatch() call - generous, but still a
// real, enforced bound. A request exceeding this is refused (Dispatch()
// returns false, a loud GTE_LOG_WARNING names the plugin + the requested
// counts + this cap, the pass is simply skipped that frame) - never a
// crash, mirroring PluginHost.cpp's own "clean skip, never crash"
// convention. Enforcement itself is PHASE2's job (the real
// PluginRenderPassBuilderAdapter_v3) - this phase only documents the
// contract via this one named constant, so no call site anywhere ever
// copy-pastes the bare literal `64`.
inline constexpr std::uint32_t kPluginComputeDispatchMaxGroupsPerDimension = 64;

// The maximum byte size of the `paramBytes`/`paramSize` blob accepted by
// IPluginCommandRecorder::Dispatch()/DrawFullscreenTriangle() below (Locked
// Architecture Decision #12, PHASE0_MASTER_STRATEGY.md) - mirrors this
// engine's own 128-byte graphics push-constant convention. A request larger
// than this is refused the same "loud warning + skip, never crash" way as
// the group-count cap above - enforcement is PHASE2's job.
inline constexpr std::size_t kPluginMaxOperationParamBytes = 128;

// --- Execute-time command recorder ------------------------------------------
//
// The curated, opaque-to-the-plugin recording surface handed to a pass's
// `execute` callback (see IPluginRenderPassBuilder_v3::ExecuteFn below).
// Never exposes a raw VkCommandBuffer/VkPipeline/VkDescriptorSet - every
// method is a plain built-in type or a PluginTextureHandle/PluginBufferHandle
// (Design Doc R12/R26).
class IPluginCommandRecorder {
public:
    virtual ~IPluginCommandRecorder() = default;

    // Binds a previously-declared (via IPluginPassSetupContext::ReadTexture/
    // WriteTexture/ReadBuffer/WriteBuffer in THIS SAME pass's setup)
    // resource to a small, fixed slot index the chosen operation's own
    // shader expects - PluginRenderOperationRegistry (PHASE2,
    // gte_core-internal) documents, per opId, which slot means what.
    virtual void BindTexture(std::uint32_t slot, PluginTextureHandle handle) = 0;
    virtual void BindBuffer(std::uint32_t slot, PluginBufferHandle handle) = 0;

    // Looks `opId` up in the host-owned PluginRenderOperationRegistry
    // (PHASE2, gte_core-internal - a plugin never sees that type, only ever
    // references an operation by its stable string id). `paramBytes`/
    // `paramSize` is a raw POD blob, capped at kPluginMaxOperationParamBytes
    // above. `groupsX`/`groupsY`/`groupsZ` mirror vkCmdDispatch()'s own 3
    // group-count arguments directly, each capped at
    // kPluginComputeDispatchMaxGroupsPerDimension above. Returns false
    // (refused, loud host-side GTE_LOG_WARNING, this dispatch simply skipped
    // this frame) for an unknown opId, an oversized paramBytes blob, or an
    // over-cap group count - never a crash. Real enforcement is PHASE2's
    // job; this phase only declares the contract.
    virtual bool Dispatch(const char* opId, const void* paramBytes, std::size_t paramSize, std::uint32_t groupsX,
        std::uint32_t groupsY, std::uint32_t groupsZ) = 0;

    // The graphics-pass sibling of Dispatch() above - draws one full-screen
    // triangle (this engine's own existing "DrawQuad" convention,
    // rg::RenderPassDrawKind::DrawQuad) using the named operation's own
    // fragment-shader equivalent. Same paramBytes/paramSize cap and refusal
    // behavior as Dispatch() above.
    virtual bool DrawFullscreenTriangle(const char* opId, const void* paramBytes, std::size_t paramSize) = 0;
};

// --- Cross-plugin blackboard-equivalent (Design Doc R18/R19) --------------

enum class PluginBlackboardValueKind : std::uint8_t {
    Texture,
    Buffer,
    Float,
    Int32,
    Float4,
};

// A plain tagged struct - never std::any/std::variant crossing the ABI,
// mirroring rg::ResourceUsage's own documented "explicit struct over
// template-heavy machinery" convention (RenderGraphTypes.h). `kind` says
// which of the fields below is actually meaningful - every other field is
// simply left at its own default and never read.
struct PluginBlackboardValue {
    PluginBlackboardValueKind kind = PluginBlackboardValueKind::Float;
    PluginTextureHandle texture;
    PluginBufferHandle buffer;
    float f = 0.0f;
    std::int32_t i = 0;
    float f4[4] = {};
};

// Published/fetched by KEY (a plain, stable, null-terminated const char*,
// never an integer enum a plugin has to coordinate with the host on) - a
// plugin picks its own namespaced key (e.g. "MyPlugin.BlurredResult") with
// no risk of silent collision with a totally different plugin's own key.
// Overwrites an existing same-key slot within one frame (mirrors
// rg::RenderPassBlackboard::Publish()'s own "last-publish-wins" rule).
// Implemented (PHASE4) as a thin forwarder to a per-frame blackboard
// instance RenderFeatureCompositor owns, gte_core-internal - a plugin only
// ever sees this interface.
class IPluginBlackboard {
public:
    virtual ~IPluginBlackboard() = default;

    virtual void Publish(const char* key, const PluginBlackboardValue& value) = 0;

    // Returns false for a never-published key OR a key published under a
    // DIFFERENT PluginBlackboardValueKind than requested - never
    // guesses/coerces between kinds.
    virtual bool Fetch(const char* key, PluginBlackboardValueKind expectedKind,
        PluginBlackboardValue& outValue) const = 0;
};

// --- The top-level generic builder ------------------------------------------
//
// A plugin implementing IRenderFeatureModule_v3 (IRenderFeatureModule.h)
// receives one of these per frame, per active view, already targeting THIS
// PLUGIN'S OWN private compositing target - exactly like
// IPluginRenderPassBuilder_v2's own equivalent contract (PHASE0_MASTER_STRATEGY.md
// Locked Product Decision #6/Locked Architecture Decision #10).
class IPluginRenderPassBuilder_v3 {
public:
    virtual ~IPluginRenderPassBuilder_v3() = default;

    // --- Resource creation -------------------------------------------------
    //
    // Creates a NEW, transient, pooled resource for THIS frame - realized,
    // host-side, through the exact same transient resource pool
    // rg::RenderGraphBuilder::CreateTexture()/CreateBuffer() already uses
    // (matched by desc VALUE EQUALITY, PHASE2's adapter calls straight
    // through - Design Doc R31). Capped at 32 CreateTexture()/CreateBuffer()
    // calls per AddRenderGraphPasses() invocation and 8192x8192 per texture
    // (Locked Architecture Decision #12, PHASE0_MASTER_STRATEGY.md) -
    // enforcement is PHASE2's job.
    virtual PluginTextureHandle CreateTexture(const char* debugName, const PluginTextureDesc& desc) = 0;
    virtual PluginBufferHandle CreateBuffer(const char* debugName, const PluginBufferDesc& desc) = 0;

    // Curated, generic, GROWABLE read access to a host-published,
    // semantically-named upstream resource. PHASE0_MASTER_STRATEGY.md Locked
    // Product Decision #3: this campaign recognizes exactly ONE semantic
    // name, "SceneColor" (the already-composited scene color for this view,
    // read-only, BEFORE this plugin's own stage runs). Returns false for any
    // other name - never a garbage handle.
    virtual bool TryGetNamedTexture(const char* semanticName, PluginTextureHandle& outHandle) = 0;

    // Returns this plugin's own already-allocated, per-(plugin, view)
    // private compositing target for THIS frame - the SAME target
    // RenderFeatureCompositor will later blend into the final image via
    // this plugin's own declared blendMode. This plugin's own pass graph
    // must WriteColorAttachment()/WriteTexture() into this exact handle at
    // least once per frame for its work to be visible at all - there is no
    // implicit/auto-detected output (PHASE0_MASTER_STRATEGY.md Locked
    // Product Decision #5).
    virtual PluginTextureHandle GetPrivateOutputTarget() = 0;

    // --- Pass declaration ---------------------------------------------------
    //
    // Function-pointer + void* userData shape (never std::function) -
    // mirrors PFN_GTE_CreatePluginModule (PluginExports.h), per
    // PublicSurface.md's ABI-boundary rule.
    using SetupFn = void (*)(IPluginPassSetupContext&, void* userData);
    using ExecuteFn = void (*)(IPluginCommandRecorder&, void* userData);

    virtual void AddGraphicsPass(const char* debugName, SetupFn setup, ExecuteFn execute, void* userData) = 0;
    virtual void AddComputePass(const char* debugName, SetupFn setup, ExecuteFn execute, void* userData) = 0;

    // --- Cross-plugin data hand-off -----------------------------------------
    virtual IPluginBlackboard& Blackboard() = 0;
};

} // namespace gte
