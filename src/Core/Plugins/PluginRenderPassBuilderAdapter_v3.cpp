#include "PluginRenderPassBuilderAdapter_v3.h"

#include "PluginRenderPassBuilderAdapterV3Validation.h"
#include "PluginRenderResourceTranslation.h"
#include "RenderFeatureCompositor.h"

#include "../Logging.h"

#include "../../Renderer/Pipeline.h"
#include "../../Renderer/Renderer.h"
// Fully specifies rg::PassContext (only forward-declared by
// RenderGraphTypes.h) - needed for ctx.resolveTexture()/resolveBuffer()/cmd/
// recordDraw below.
#include "../../Renderer/RenderGraph/RenderGraph.h"

#include <array>
#include <atomic>
#include <cstring>
#include <optional>
#include <utility>

namespace gte {

namespace {

// PHASE0_MASTER_STRATEGY.md Step 2.5/Step 3.4 - a process-wide, monotonic
// counter incremented once PER ADAPTER CONSTRUCTION (never per frame, never
// reset, never per-handle) - two adapters built in two different frames (or
// for two different plugins the same frame) NEVER share a generation value.
// Sized to match PluginTextureHandle::generation/PluginBufferHandle::generation's
// own real ABI field width (std::uint32_t, plugins/gte_plugin_abi/
// PluginRenderResource.h) - NOT std::uint64_t, since assigning a wider
// counter into that 32-bit field would silently truncate; a 32-bit counter
// wraps only after ~4 billion adapter constructions, which this campaign's
// own live smoke tests (a few hundred to a few thousand frames) never come
// remotely close to.
std::atomic<std::uint32_t> s_nextAdapterGeneration{ 1 };

} // namespace

PluginRenderPassBuilderAdapter_v3::PluginRenderPassBuilderAdapter_v3(rg::RenderGraphBuilder& builder,
    rg::TextureHandle privateTarget, PluginRenderOperationRegistry& operationRegistry, IPluginBlackboard& blackboard,
    rg::TextureHandle sceneColorTarget, VkSampler sceneColorSampler, RenderFeatureCompositor& compositor,
    std::string opDescriptorSetKeyPrefix)
    : m_builder(builder)
    , m_privateTarget(privateTarget)
    , m_operationRegistry(operationRegistry)
    , m_blackboard(blackboard)
    , m_sceneColorTarget(sceneColorTarget)
    , m_sceneColorSampler(sceneColorSampler)
    , m_compositor(compositor)
    , m_opDescriptorSetKeyPrefix(std::move(opDescriptorSetKeyPrefix))
    , m_state(std::make_shared<TranslationState>())
{
    m_state->generation = s_nextAdapterGeneration.fetch_add(1);
}

const PluginRenderPassBuilderAdapter_v3::TranslatedTexture* PluginRenderPassBuilderAdapter_v3::ResolveTranslatedTextureIn(
    const TranslationState& state, PluginTextureHandle handle) noexcept
{
    if (!IsPluginResourceHandleValid(handle.generation, handle.index, state.generation, state.textures.size())) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            "rejected a PluginTextureHandle that is out-of-range or from a stale/foreign adapter instance "
            "(generation mismatch) - a plugin must never hold a handle across frames or use one it did not "
            "receive from THIS call's own IPluginPassSetupContext/IPluginCommandRecorder.");
        return nullptr;
    }
    return &state.textures[handle.index];
}

const rg::BufferHandle* PluginRenderPassBuilderAdapter_v3::ResolveBufferIn(
    const TranslationState& state, PluginBufferHandle handle) noexcept
{
    if (!IsPluginResourceHandleValid(handle.generation, handle.index, state.generation, state.buffers.size())) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            "rejected a PluginBufferHandle that is out-of-range or from a stale/foreign adapter instance "
            "(generation mismatch) - a plugin must never hold a handle across frames or use one it did not "
            "receive from THIS call's own IPluginPassSetupContext/IPluginCommandRecorder.");
        return nullptr;
    }
    return &state.buffers[handle.index];
}

PluginTextureHandle PluginRenderPassBuilderAdapter_v3::CreateTexture(const char* debugName, const PluginTextureDesc& desc)
{
    if (!IsWithinResourceCreationCountCap(m_resourceCreationCallCount)) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("CreateTexture(\"") + (debugName != nullptr ? debugName : "?")
            + "\") refused - this plugin already created " + std::to_string(m_resourceCreationCallCount)
            + " resources this AddRenderGraphPasses() invocation, exceeding the 32-resource-per-invocation cap.");
        return PluginTextureHandle{};
    }
    if (!IsWithinTextureDimensionCap(desc.width, desc.height)) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("CreateTexture(\"") + (debugName != nullptr ? debugName : "?") + "\") refused - "
            + std::to_string(desc.width) + "x" + std::to_string(desc.height)
            + " exceeds the 8192x8192 per-texture dimension cap.");
        return PluginTextureHandle{};
    }

    ++m_resourceCreationCallCount;
    const rg::TextureDesc rgDesc = ToRgTextureDesc(desc);
    const rg::TextureHandle handle = m_builder.CreateTexture(debugName, rgDesc);
    m_state->textures.push_back(TranslatedTexture{ handle, VK_NULL_HANDLE, false });
    return PluginTextureHandle{ static_cast<std::uint32_t>(m_state->textures.size() - 1), m_state->generation };
}

PluginBufferHandle PluginRenderPassBuilderAdapter_v3::CreateBuffer(const char* debugName, const PluginBufferDesc& desc)
{
    if (!IsWithinResourceCreationCountCap(m_resourceCreationCallCount)) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("CreateBuffer(\"") + (debugName != nullptr ? debugName : "?")
            + "\") refused - this plugin already created " + std::to_string(m_resourceCreationCallCount)
            + " resources this AddRenderGraphPasses() invocation, exceeding the 32-resource-per-invocation cap.");
        return PluginBufferHandle{};
    }

    ++m_resourceCreationCallCount;
    const rg::BufferDesc rgDesc = ToRgBufferDesc(desc);
    const rg::BufferHandle handle = m_builder.CreateBuffer(debugName, rgDesc);
    m_state->buffers.push_back(handle);
    return PluginBufferHandle{ static_cast<std::uint32_t>(m_state->buffers.size() - 1), m_state->generation };
}

bool PluginRenderPassBuilderAdapter_v3::TryGetNamedTexture(const char* semanticName, PluginTextureHandle& outHandle)
{
    if (semanticName == nullptr || std::strcmp(semanticName, "SceneColor") != 0) {
        return false;
    }

    if (!m_sceneColorCached) {
        // PHASE0_MASTER_STRATEGY.md Step 2.5 - the sampler override is set
        // HERE, from the constructor-supplied `m_sceneColorSampler`
        // (resolved->sampler, per RenderFeatureCompositor::
        // ContributeRenderGraphPasses()) - NEVER left null. `m_sceneColorTarget`
        // is always an IMPORTED handle, whose own ctx-resolved sampler is
        // always VK_NULL_HANDLE (RenderGraph.cpp's EnsureTextureResolved()).
        m_state->textures.push_back(TranslatedTexture{ m_sceneColorTarget, m_sceneColorSampler, false });
        m_cachedSceneColor =
            PluginTextureHandle{ static_cast<std::uint32_t>(m_state->textures.size() - 1), m_state->generation };
        m_sceneColorCached = true;
    }
    outHandle = m_cachedSceneColor;
    return true;
}

PluginTextureHandle PluginRenderPassBuilderAdapter_v3::GetPrivateOutputTarget()
{
    if (!m_privateOutputTargetCached) {
        m_state->textures.push_back(TranslatedTexture{ m_privateTarget, VK_NULL_HANDLE, /*isPrivateOutputTarget=*/true });
        m_cachedPrivateOutputTarget =
            PluginTextureHandle{ static_cast<std::uint32_t>(m_state->textures.size() - 1), m_state->generation };
        m_privateOutputTargetCached = true;
    }
    return m_cachedPrivateOutputTarget;
}

void PluginRenderPassBuilderAdapter_v3::AddGraphicsPass(
    const char* debugName, SetupFn setup, ExecuteFn execute, void* userData)
{
    m_builder.AddRenderPass(debugName, rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [this, setup, userData](rg::RenderGraphBuilder::PassBuilder& pass) {
            // SAFE: this lambda runs SYNCHRONOUSLY, before AddRenderPass()
            // itself returns - see this file's own top-of-file doc comment.
            SetupContextAdapter ctxAdapter(*this, pass);
            if (setup != nullptr) {
                setup(ctxAdapter, userData);
            }
            // editor-core-separation-9 campaign, PHASE3
            // (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md) - a
            // FIFTH, real hazard found while implementing this phase's own
            // gte.builtin.blit_fullscreen (the first real
            // DrawFullscreenTriangle-kind operation): its graphics Pipeline
            // is built through the standard, shared `Pipeline` class
            // (Renderer/Pipeline.h), which ALWAYS unconditionally enables a
            // real depth test (VK_COMPARE_OP_LESS) with no way to opt out
            // (see Pipeline.h's own class comment) - confirmed by direct
            // code reading, the EXACT same finding
            // src/Editor/GBufferValidation.cpp's own header comment already
            // documents for the identical underlying shader technique. A
            // `_v3` plugin's own ABI (IPluginPassSetupContext) has NO method
            // to declare a depth attachment itself (the Design Doc's own
            // curated PluginResourceAccess vocabulary never included one -
            // PHASE0_MASTER_STRATEGY.md Step 2.4), so this adapter
            // transparently attaches a scratch/unused depth-stencil write
            // against THIS PLUGIN'S OWN private output target's companion
            // depth buffer (every RenderTexture always owns one -
            // RenderTexture.h/RenderFeatureCompositor's own
            // EnsurePrivateTargetState()) - cleared to 1.0f every time,
            // mirroring GBufferValidation.cpp's own identical
            // kGBufferScratchClearDepth=1.0f workaround exactly (a real,
            // hand-verified value that guarantees this op's own full-screen
            // triangle, drawn at a fixed NDC z=0.0, always survives the
            // mandatory VK_COMPARE_OP_LESS test: 0.0 < 1.0). Safe for THIS
            // CAMPAIGN'S OWN SCOPE, where the only registered
            // DrawFullscreenTriangle-kind operation always draws its real
            // color output into GetPrivateOutputTarget() (Locked Product
            // Decision #5) - never a texture minted via CreateTexture().
            pass.WriteDepthStencilAttachment(m_privateTarget, 1.0f);
        },
        // NOT SAFE to capture `this` here - see this file's own top-of-file
        // "FOURTH hazard" doc comment. Every captured name below is either a
        // plain value/pointer, a BY-VALUE COPY of a shared_ptr/std::string,
        // or a reference bound directly to a LONG-LIVED, Core-owned
        // singleton at LAMBDA-CREATION time (never through `this`).
        [state = m_state, &operationRegistry = m_operationRegistry, &compositor = m_compositor,
            keyPrefix = m_opDescriptorSetKeyPrefix, execute, userData, debugName](rg::PassContext& ctx) {
            CommandRecorderAdapter recorderAdapter(state, operationRegistry, compositor, keyPrefix, ctx, debugName);
            if (execute != nullptr) {
                execute(recorderAdapter, userData);
            }
        },
        // PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.4 - the
        // explicit trailing rg::RenderPassEvent::AfterEverything argument is
        // REQUIRED, not optional or defaulted - see that section for the
        // full correctness reasoning. DrawQuad is this method's own best,
        // purely-descriptive guess (this is a graphics pass, and this
        // campaign's own first real graphics-kind _v3 consumer, PHASE3's
        // gte.builtin.blit_fullscreen, IS a full-screen-triangle draw) -
        // never load-bearing (RenderPassDrawKind is read by nothing in
        // RenderGraph.cpp/RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp).
        rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterEverything);
}

void PluginRenderPassBuilderAdapter_v3::AddComputePass(
    const char* debugName, SetupFn setup, ExecuteFn execute, void* userData)
{
    m_builder.AddRenderPass(debugName, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [this, setup, userData](rg::RenderGraphBuilder::PassBuilder& pass) {
            // SAFE - see AddGraphicsPass()'s own identical note above.
            SetupContextAdapter ctxAdapter(*this, pass);
            if (setup != nullptr) {
                setup(ctxAdapter, userData);
            }
        },
        // NOT SAFE to capture `this` - see AddGraphicsPass()'s own identical
        // note above (this file's top-of-file "FOURTH hazard" doc comment).
        [state = m_state, &operationRegistry = m_operationRegistry, &compositor = m_compositor,
            keyPrefix = m_opDescriptorSetKeyPrefix, execute, userData, debugName](rg::PassContext& ctx) {
            CommandRecorderAdapter recorderAdapter(state, operationRegistry, compositor, keyPrefix, ctx, debugName);
            if (execute != nullptr) {
                execute(recorderAdapter, userData);
            }
        },
        // PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 2.4 - see
        // AddGraphicsPass()'s own identical note above. drawKind is
        // meaningless for a Compute-kind pass (RenderPassDrawKind's own doc
        // comment, RenderGraphTypes.h) - left at the default.
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
}

IPluginBlackboard& PluginRenderPassBuilderAdapter_v3::Blackboard()
{
    return m_blackboard;
}

// --- SetupContextAdapter ----------------------------------------------------

void PluginRenderPassBuilderAdapter_v3::SetupContextAdapter::ReadTexture(
    PluginTextureHandle handle, PluginResourceAccess access)
{
    const TranslatedTexture* translated = m_owner.ResolveTranslatedTexture(handle);
    if (translated == nullptr) {
        return;
    }
    m_pass.ReadTexture(translated->handle, ToRgAccess(access));
}

void PluginRenderPassBuilderAdapter_v3::SetupContextAdapter::WriteTexture(
    PluginTextureHandle handle, PluginResourceAccess access)
{
    const TranslatedTexture* translated = m_owner.ResolveTranslatedTexture(handle);
    if (translated == nullptr) {
        return;
    }
    m_pass.WriteTexture(translated->handle, ToRgAccess(access));
}

void PluginRenderPassBuilderAdapter_v3::SetupContextAdapter::ReadBuffer(
    PluginBufferHandle handle, PluginResourceAccess access)
{
    const rg::BufferHandle* resolved = m_owner.ResolveBuffer(handle);
    if (resolved == nullptr) {
        return;
    }
    m_pass.ReadBuffer(*resolved, ToRgAccess(access));
}

void PluginRenderPassBuilderAdapter_v3::SetupContextAdapter::WriteBuffer(
    PluginBufferHandle handle, PluginResourceAccess access)
{
    const rg::BufferHandle* resolved = m_owner.ResolveBuffer(handle);
    if (resolved == nullptr) {
        return;
    }
    m_pass.WriteBuffer(*resolved, ToRgAccess(access));
}

void PluginRenderPassBuilderAdapter_v3::SetupContextAdapter::WriteColorAttachment(
    PluginTextureHandle handle, bool hasClearColor, float r, float g, float b, float a)
{
    const TranslatedTexture* translated = m_owner.ResolveTranslatedTexture(handle);
    if (translated == nullptr) {
        return;
    }
    m_pass.WriteColorAttachment(
        translated->handle, hasClearColor ? std::optional<std::array<float, 4>>{ { r, g, b, a } } : std::nullopt);
}

// --- CommandRecorderAdapter --------------------------------------------------

void PluginRenderPassBuilderAdapter_v3::CommandRecorderAdapter::BindTexture(
    std::uint32_t slot, PluginTextureHandle handle)
{
    if (slot >= kMaxSlots) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            "BindTexture: slot " + std::to_string(slot) + " is out of range (max " + std::to_string(kMaxSlots - 1)
            + ") - ignored.");
        return;
    }
    if (PluginRenderPassBuilderAdapter_v3::ResolveTranslatedTextureIn(*m_state, handle) == nullptr) {
        return; // already logged - this slot is left unbound, naturally caught by Dispatch()'s own step 5.
    }
    m_slotBindings[slot] = SlotBinding{ /*hasValue=*/true, /*isBuffer=*/false, handle, PluginBufferHandle{} };
}

void PluginRenderPassBuilderAdapter_v3::CommandRecorderAdapter::BindBuffer(
    std::uint32_t slot, PluginBufferHandle handle)
{
    if (slot >= kMaxSlots) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            "BindBuffer: slot " + std::to_string(slot) + " is out of range (max " + std::to_string(kMaxSlots - 1)
            + ") - ignored.");
        return;
    }
    if (PluginRenderPassBuilderAdapter_v3::ResolveBufferIn(*m_state, handle) == nullptr) {
        return; // already logged.
    }
    m_slotBindings[slot] = SlotBinding{ /*hasValue=*/true, /*isBuffer=*/true, PluginTextureHandle{}, handle };
}

const PluginRenderOpInfo* PluginRenderPassBuilderAdapter_v3::CommandRecorderAdapter::ValidateCommon(
    const char* opId, PluginRenderOpKind expectedKind, std::size_t paramSize) const
{
    if (opId == nullptr) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3", "Dispatch/DrawFullscreenTriangle refused - null opId.");
        return nullptr;
    }

    const PluginRenderOpInfo* op = m_operationRegistry.Find(opId);
    if (op == nullptr) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("refused - unknown opId \"") + opId + "\" (pass \"" + (m_debugName != nullptr ? m_debugName : "?")
            + "\").");
        return nullptr;
    }
    if (op->kind != expectedKind) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("refused - opId \"") + opId + "\" is registered as a "
            + (op->kind == PluginRenderOpKind::Compute ? "Compute" : "DrawFullscreenTriangle")
            + "-kind operation - call the matching method instead.");
        return nullptr;
    }
    if (!IsExactParamSizeMatch(paramSize, op->maxParamBytes)) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("refused - opId \"") + opId + "\" expects exactly " + std::to_string(op->maxParamBytes)
            + " paramBytes, got " + std::to_string(paramSize) + ".");
        return nullptr;
    }

    for (std::size_t i = 0; i < op->slots.size(); ++i) {
        const SlotBinding& binding = m_slotBindings[i];
        bool boundIsPrivateOutputTarget = false;
        if (binding.hasValue && !binding.isBuffer) {
            const TranslatedTexture* translated =
                PluginRenderPassBuilderAdapter_v3::ResolveTranslatedTextureIn(*m_state, binding.texture);
            boundIsPrivateOutputTarget = (translated != nullptr && translated->isPrivateOutputTarget);
        }
        const bool isCombinedImageSamplerSlot = (op->slots[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        const PluginOpSlotBindingResult result = ValidatePluginOpSlotBinding(
            op->slots[i].isBuffer, binding.hasValue, binding.isBuffer, isCombinedImageSamplerSlot,
            boundIsPrivateOutputTarget);
        if (result != PluginOpSlotBindingResult::Ok) {
            const char* reason = result == PluginOpSlotBindingResult::Unbound
                ? "unbound"
                : result == PluginOpSlotBindingResult::WrongKind
                ? "wrong resource kind bound (a texture bound against a buffer slot, or vice versa)"
                : "a plugin's own private output target cannot be sampled back within the same frame";
            GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
                std::string("refused - opId \"") + opId + "\" slot " + std::to_string(i) + " binding is invalid ("
                + reason + ").");
            return nullptr;
        }
    }
    return op;
}

VkDescriptorSet PluginRenderPassBuilderAdapter_v3::CommandRecorderAdapter::BuildAndRewriteDescriptorSet(
    const PluginRenderOpInfo& op) const
{
    std::vector<ComputeDescriptorWrite> writes;
    writes.reserve(op.slots.size());
    for (std::size_t i = 0; i < op.slots.size(); ++i) {
        const SlotBinding& binding = m_slotBindings[i];
        const PluginRenderOpSlot& slot = op.slots[i];
        const std::uint32_t bindingIndex = static_cast<std::uint32_t>(i);

        if (slot.isBuffer) {
            const rg::BufferHandle* resolved =
                PluginRenderPassBuilderAdapter_v3::ResolveBufferIn(*m_state, binding.buffer);
            const VkBuffer buffer = resolved != nullptr ? m_ctx.resolveBuffer(*resolved) : VK_NULL_HANDLE;
            writes.push_back(ComputeDescriptorWrite::StorageBuffer(bindingIndex, buffer));
            continue;
        }

        const TranslatedTexture* translated =
            PluginRenderPassBuilderAdapter_v3::ResolveTranslatedTextureIn(*m_state, binding.texture);
        const rg::PassContext::ResolvedTexture resolvedTexture =
            translated != nullptr ? m_ctx.resolveTexture(translated->handle) : rg::PassContext::ResolvedTexture{};

        if (slot.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            // PHASE0_MASTER_STRATEGY.md Step 2.5 - prefer the translated
            // entry's own external sampler override (set for "SceneColor")
            // whenever present; otherwise fall back to ctx's own resolved
            // sampler (correct for a CreateTexture()-minted transient handle
            // - see that section for exactly why this two-way fallback is
            // required, not merely one or the other).
            const VkSampler sampler = (translated != nullptr && translated->externalSamplerOverride != VK_NULL_HANDLE)
                ? translated->externalSamplerOverride
                : resolvedTexture.sampler;
            writes.push_back(ComputeDescriptorWrite::CombinedImageSampler(bindingIndex, resolvedTexture.view, sampler));
        } else {
            writes.push_back(ComputeDescriptorWrite::StorageImage(bindingIndex, resolvedTexture.view));
        }
    }

    // PHASE0_MASTER_STRATEGY.md Step 2.6/PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md
    // Step 2.6 - the PERSISTENT, RenderFeatureCompositor-owned descriptor-set
    // cache, keyed by (plugin, view, pass debugName) - NEVER cached on this
    // adapter instance itself (a fresh adapter is constructed every frame).
    const std::string key = m_opDescriptorSetKeyPrefix + (m_debugName != nullptr ? m_debugName : "?");
    ComputeDescriptorSet& descriptorSet = m_compositor.EnsureV3OpDescriptorSet(key, op.descriptorSetLayout);
    descriptorSet.Rewrite(m_operationRegistry.GetDevice(), writes);
    return descriptorSet.Native();
}

bool PluginRenderPassBuilderAdapter_v3::CommandRecorderAdapter::Dispatch(const char* opId, const void* paramBytes,
    std::size_t paramSize, std::uint32_t groupsX, std::uint32_t groupsY, std::uint32_t groupsZ)
{
    const PluginRenderOpInfo* op = ValidateCommon(opId, PluginRenderOpKind::Compute, paramSize);
    if (op == nullptr) {
        return false;
    }
    if (!IsWithinDispatchGroupCountCap(groupsX, groupsY, groupsZ)) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("Dispatch refused - opId \"") + opId + "\" requested group counts (" + std::to_string(groupsX)
            + "," + std::to_string(groupsY) + "," + std::to_string(groupsZ) + ") exceed the per-dimension cap of "
            + std::to_string(kPluginComputeDispatchMaxGroupsPerDimension) + ".");
        return false;
    }

    const VkDescriptorSet descriptorSet = BuildAndRewriteDescriptorSet(*op);

    // Copy into a local, zeroed, fixed-size scratch buffer - never pass the
    // caller's own pointer straight into Renderer::Dispatch() without a
    // bounds-checked copy first.
    unsigned char scratch[kPluginMaxOperationParamBytes] = {};
    if (op->maxParamBytes > 0 && paramBytes != nullptr) {
        std::memcpy(scratch, paramBytes, op->maxParamBytes);
    }

    // PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md Step 3.4, point 8 - a
    // plugin calling gte.builtin.solid_fill/radial_vignette/color_grade
    // never has to know an `opCode` exists at all; the adapter stamps it in
    // transparently, from the registry's own PluginRenderOpInfo::opCode
    // field, ONLY for entries sharing the shared "uber ops" pipeline
    // (RenderFeatureOps.comp) - detected here by POINTER IDENTITY against
    // the registry's own OpsPipeline() accessor, never a hardcoded id list.
    if (op->computePipeline == &m_operationRegistry.OpsPipeline()) {
        const float opCodeAsFloat = static_cast<float>(op->opCode);
        static_assert(sizeof(scratch) >= sizeof(float), "scratch buffer must fit at least one float");
        std::memcpy(scratch, &opCodeAsFloat, sizeof(float));
    }

    // task_manager/better-render-pass-1 campaign, PHASE7
    // (PHASE7_MIGRATE_PLUGIN_RENDER_OPERATION_REGISTRY.md, Step 2.3) - migrated
    // onto rg::CommandBuffer: m_ctx (a real rg::PassContext&) is genuinely
    // available at this call site, so this is a clean, low-risk,
    // ABI-surface-safe internal change - BeginGraphPassRecording()/
    // EndGraphPassRecording() are no longer called directly; CommandBuffer::
    // Dispatch() opens/closes that exact same bracket internally.
    rg::CommandBuffer cmd = m_ctx.Cmd();
    cmd.BindComputePipeline(*op->computePipeline);
    cmd.BindDescriptorSet(descriptorSet);
    cmd.SetPushConstants(scratch, static_cast<std::uint32_t>(op->maxParamBytes));
    cmd.Dispatch(groupsX, groupsY, groupsZ);
    return true;
}

bool PluginRenderPassBuilderAdapter_v3::CommandRecorderAdapter::DrawFullscreenTriangle(
    const char* opId, const void* paramBytes, std::size_t paramSize)
{
    const PluginRenderOpInfo* op = ValidateCommon(opId, PluginRenderOpKind::DrawFullscreenTriangle, paramSize);
    if (op == nullptr) {
        return false;
    }

    // editor-core-separation-9 campaign, PHASE3
    // (PHASE3_GENERIC_RESOURCE_PLUMBING_AND_BLUR_DEMO_PROOF.md) -
    // `gte.builtin.blit_fullscreen` is this campaign's first real
    // DrawFullscreenTriangle-kind registrant (PluginRenderOperationRegistry::
    // RegisterBlitFullscreen()), so this branch is genuinely reachable now -
    // kept as a real, defensive guard regardless (a future registry entry
    // whose own registration failed partway through, leaving
    // graphicsPipeline null, must still be refused loudly here rather than
    // dereferencing a null pointer below).
    if (op->graphicsPipeline == nullptr) {
        GTE_LOG_WARNING("PluginRenderPassBuilderAdapter_v3",
            std::string("DrawFullscreenTriangle refused - opId \"") + opId
            + "\" has no registered graphics pipeline.");
        return false;
    }

    const VkDescriptorSet descriptorSet = BuildAndRewriteDescriptorSet(*op);

    unsigned char scratch[kPluginMaxOperationParamBytes] = {};
    if (op->maxParamBytes > 0 && paramBytes != nullptr) {
        std::memcpy(scratch, paramBytes, op->maxParamBytes);
    }

    // Mirrors AtmosphereSkyBackgroundRenderer::Draw()'s own established
    // "one real, hand-verified 3-vertex full-screen-triangle vkCmdDraw()"
    // recording shape - a fixed-function draw with a single descriptor set
    // and a fragment-stage-only push-constant block, issued directly against
    // this pass's own ctx.cmd (no Renderer::Submit()/BeginGraphPassRecording()
    // bracket involved, mirroring that exact precedent).
    vkCmdBindPipeline(m_ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, op->graphicsPipeline->Native());
    vkCmdBindDescriptorSets(
        m_ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, op->graphicsPipeline->Layout(), 0, 1, &descriptorSet, 0, nullptr);
    // editor-core-separation-9 campaign, PHASE3 - unlike
    // AtmosphereSkyBackgroundRenderer's own raw, hand-rolled VkPipeline
    // (which declares NO vertex input state at all), this op's own
    // graphicsPipeline is built through the standard, shared `Pipeline`
    // class (Renderer/Pipeline.h), which ALWAYS declares a real
    // VertexLayout::PositionColor vertex-input binding with no way to opt
    // out - so SOMETHING real must be bound at that binding number before
    // this draw, or it is invalid Vulkan usage (mirrors
    // src/Editor/GBufferValidation.cpp's own identical "m_dummyTriangle"
    // workaround exactly - see PluginRenderOperationRegistry::
    // RegisterBlitFullscreen()'s own doc comment for the full reasoning).
    // This op's own vertex shader (Shaders/PluginBlitFullscreen.vert) never
    // reads this data - it derives a full-screen triangle purely from
    // gl_VertexIndex.
    if (op->dummyVertexBuffer != VK_NULL_HANDLE) {
        const VkBuffer vertexBuffer = op->dummyVertexBuffer;
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(m_ctx.cmd, 0, 1, &vertexBuffer, &offset);
    }
    if (op->maxParamBytes > 0) {
        vkCmdPushConstants(m_ctx.cmd, op->graphicsPipeline->Layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
            static_cast<std::uint32_t>(op->maxParamBytes), scratch);
    }
    vkCmdDraw(m_ctx.cmd, 3, 1, 0, 0);
    return true;
}

} // namespace gte
