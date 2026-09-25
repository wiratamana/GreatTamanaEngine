#include "RenderFeatureCompositor.h"

#include "PluginRenderPassBuilderAdapter_v2.h"
#include "PluginRenderPassBuilderAdapter_v3.h"

#include "../Core.h"
#include "../Logging.h"

#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h"

#include "../../Renderer/ComputeDispatch.h"
#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace gte {

namespace {

// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - file-local helpers used ONLY by
// RenderFeatureCompositor::DebugSnapshot() below, mirroring
// RenderGraphTypes.cpp's own ToString(RenderPassEvent)/ToString(RenderPassDrawKind)
// precedent exactly: deliberately NO `default:` case, so a future new
// enumerator fails to compile-warn (this codebase enables no `-Wswitch`, per
// AGENTS.md's own "Render Pass System" section, but the discipline is kept
// anyway) rather than silently falling through - "Unknown" is the one
// deliberate fallback return.
const char* ToString(RenderFeatureStage stage) noexcept
{
    switch (stage) {
    case RenderFeatureStage::PreOpaque:
        return "PreOpaque";
    case RenderFeatureStage::PostOpaque:
        return "PostOpaque";
    case RenderFeatureStage::PostTransparent:
        return "PostTransparent";
    case RenderFeatureStage::PostComposite:
        return "PostComposite";
    case RenderFeatureStage::PreUI:
        return "PreUI";
    }
    return "Unknown";
}

const char* ToString(RenderFeatureBlendMode blendMode) noexcept
{
    switch (blendMode) {
    case RenderFeatureBlendMode::Replace:
        return "Replace";
    case RenderFeatureBlendMode::AlphaOver:
        return "AlphaOver";
    case RenderFeatureBlendMode::Additive:
        return "Additive";
    case RenderFeatureBlendMode::Multiply:
        return "Multiply";
    case RenderFeatureBlendMode::ScreenSpaceMask:
        return "ScreenSpaceMask";
    }
    return "Unknown";
}

// editor-core-separation-9 campaign, PHASE4
// (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.1) - file-local
// helpers used ONLY by RenderFeatureCompositor::BlackboardAdapter::Publish()/
// Fetch() (below) to build a human-readable diagnostic log line - the plugin
// ABI (IPluginRenderPassBuilder_v3.h) has no logging capability of its own,
// so this is the ONLY place a plugin's own Publish()/Fetch() call is ever
// externally confirmable (GET /get_logs). Mirrors this same file's own
// ToString(RenderFeatureStage)/ToString(RenderFeatureBlendMode) precedent
// immediately above: deliberately NO `default:` case.
const char* ToString(PluginBlackboardValueKind kind) noexcept
{
    switch (kind) {
    case PluginBlackboardValueKind::Texture:
        return "Texture";
    case PluginBlackboardValueKind::Buffer:
        return "Buffer";
    case PluginBlackboardValueKind::Float:
        return "Float";
    case PluginBlackboardValueKind::Int32:
        return "Int32";
    case PluginBlackboardValueKind::Float4:
        return "Float4";
    }
    return "Unknown";
}

std::string DescribeBlackboardValue(const PluginBlackboardValue& value)
{
    switch (value.kind) {
    case PluginBlackboardValueKind::Texture:
        return "texture{index=" + std::to_string(value.texture.index)
            + ",generation=" + std::to_string(value.texture.generation) + "}";
    case PluginBlackboardValueKind::Buffer:
        return "buffer{index=" + std::to_string(value.buffer.index)
            + ",generation=" + std::to_string(value.buffer.generation) + "}";
    case PluginBlackboardValueKind::Float:
        return "f=" + std::to_string(value.f);
    case PluginBlackboardValueKind::Int32:
        return "i=" + std::to_string(value.i);
    case PluginBlackboardValueKind::Float4:
        return "f4=[" + std::to_string(value.f4[0]) + "," + std::to_string(value.f4[1]) + ","
            + std::to_string(value.f4[2]) + "," + std::to_string(value.f4[3]) + "]";
    }
    return "?";
}

} // namespace

// editor-core-separation-9 campaign, PHASE4 - the REAL IPluginBlackboard
// implementation, replacing PHASE2's NoOpPluginBlackboard stand-in. See
// RenderFeatureCompositor.h's own doc comment (BlackboardAdapter) for the
// full contract/reasoning.
void RenderFeatureCompositor::BlackboardAdapter::Publish(const char* key, const PluginBlackboardValue& value)
{
    if (key == nullptr) {
        return; // defensive - mirrors this ABI's own "never guesses/coerces" discipline elsewhere.
    }
    m_owner.m_blackboard[key] = value; // last-publish-wins, mirrors rg::RenderPassBlackboard::Publish()'s own rule.

    if (m_owner.m_blackboardLoggedPublishKeys.insert(key).second) {
        GTE_LOG_INFO("RenderFeatureCompositor.Blackboard",
            std::string("Published key '") + key + "' kind=" + ToString(value.kind) + " "
            + DescribeBlackboardValue(value));
    }
}

bool RenderFeatureCompositor::BlackboardAdapter::Fetch(
    const char* key, PluginBlackboardValueKind expectedKind, PluginBlackboardValue& outValue) const
{
    if (key == nullptr) {
        return false;
    }

    const auto it = m_owner.m_blackboard.find(key);
    if (it == m_owner.m_blackboard.end()) {
        GTE_LOG_WARNING("RenderFeatureCompositor.Blackboard",
            std::string("Fetch failed - key '") + key + "' was never published this frame.");
        return false;
    }
    if (it->second.kind != expectedKind) {
        GTE_LOG_WARNING("RenderFeatureCompositor.Blackboard",
            std::string("Fetch failed - key '") + key + "' was published as kind=" + ToString(it->second.kind)
            + " but fetched as kind=" + ToString(expectedKind) + " - never guesses/coerces between kinds.");
        return false;
    }

    outValue = it->second;
    if (m_owner.m_blackboardLoggedFetchSuccessKeys.insert(key).second) {
        GTE_LOG_INFO("RenderFeatureCompositor.Blackboard",
            std::string("Fetch succeeded - key '") + key + "' kind=" + ToString(outValue.kind) + " "
            + DescribeBlackboardValue(outValue));
    }
    return true;
}

RenderFeatureCompositor::RenderFeatureCompositor(
    Core& core, Renderer& renderer, PluginRenderOperationRegistry& operationRegistry)
    : m_core(core)
    , m_renderer(renderer)
    , m_operationRegistry(operationRegistry)
    , m_blackboardAdapter(*this)
{
}

// editor-core-separation-8 campaign, PHASE2 - extracted VERBATIM from
// OnPluginsLoaded()'s own former inline `sortAndDetectCollisions` lambda
// (zero behavior change), so SetFeaturePriority() can reuse the exact same
// sort+collision-tie-break logic for a single re-sort after a live priority
// change, without duplicating it.
void RenderFeatureCompositor::SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName)
{
    std::stable_sort(entries.begin(), entries.end(),
        [](const Entry& a, const Entry& b) { return a.descriptor.priority < b.descriptor.priority; });

    std::size_t i = 0;
    while (i < entries.size()) {
        std::size_t j = i;
        while (j + 1 < entries.size() && entries[j + 1].descriptor.priority == entries[i].descriptor.priority) {
            ++j;
        }
        if (j > i) {
            // entries[i..j] all declared the identical priority within
            // this stage - an ambiguous, but never-crashing, situation
            // (PHASE0_MASTER_STRATEGY.md's own Locked Design Decision,
            // mirroring the Proposal's Section 3.4 step 3).
            for (std::size_t k = i; k < j; ++k) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    std::string(entries[k].descriptor.name) + " and " + entries[k + 1].descriptor.name
                    + " both declared priority " + std::to_string(entries[k].descriptor.priority) + " in stage "
                    + stageName + " - this is ambiguous; falling back to a stable, lexical name tie-break. "
                    "Assign each plugin a distinct priority to remove this warning.");
            }
            std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                entries.begin() + static_cast<std::ptrdiff_t>(j) + 1, [](const Entry& a, const Entry& b) {
                    return std::strcmp(a.descriptor.name, b.descriptor.name) < 0;
                });
        }
        i = j + 1;
    }
}

// editor-core-separation-8 campaign, PHASE2 - shared lookup used by both
// SetFeatureEnabled() and SetFeaturePriority(): searches m_postComposite then
// m_preUi for an Entry whose descriptor.name matches `name` exactly. Returns
// nullptr if not found.
RenderFeatureCompositor::Entry* RenderFeatureCompositor::FindEntryByName(const std::string& name)
{
    for (Entry& entry : m_postComposite) {
        if (name == entry.descriptor.name) {
            return &entry;
        }
    }
    for (Entry& entry : m_preUi) {
        if (name == entry.descriptor.name) {
            return &entry;
        }
    }
    return nullptr;
}

// editor-core-separation-8 campaign, PHASE2 - host-side enable/disable
// override. See RenderFeatureCompositor.h's own doc comment for the full
// contract.
bool RenderFeatureCompositor::SetFeatureEnabled(const std::string& name, bool enabled)
{
    Entry* entry = FindEntryByName(name);
    if (entry == nullptr) {
        return false;
    }
    entry->enabledOverride = enabled;
    return true;
}

// editor-core-separation-8 campaign, PHASE2 - host-side LIVE priority
// override. See RenderFeatureCompositor.h's own doc comment for the full
// contract.
bool RenderFeatureCompositor::SetFeaturePriority(const std::string& name, std::int32_t priority)
{
    Entry* entry = FindEntryByName(name);
    if (entry == nullptr) {
        return false;
    }
    entry->descriptor.priority = priority;

    // Re-sort ONLY the stage this entry actually belongs to - determined by
    // which vector FindEntryByName() actually found it in, not by
    // entry->descriptor.stage alone (defensive: always re-derive from the
    // real container to stay correct even if this class's own stage-routing
    // rules ever change). NOTE: the re-sort below invalidates `entry` itself
    // (std::stable_sort may reorder/relocate elements) - it is never
    // dereferenced again after this point.
    const bool isPostComposite =
        std::find_if(m_postComposite.begin(), m_postComposite.end(),
            [&name](const Entry& e) { return name == e.descriptor.name; })
        != m_postComposite.end();
    if (isPostComposite) {
        SortAndDetectCollisionsInStage(m_postComposite, "PostComposite");
    } else {
        SortAndDetectCollisionsInStage(m_preUi, "PreUI");
    }
    return true;
}

// editor-core-separation-6 campaign, PHASE4 (Step 3.4) - discovers every
// loaded IRenderFeatureModule_v2/_v3, snapshots its descriptor exactly once,
// refuses (loudly) any module declaring an unwired stage
// (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1), then sorts each
// stage's own surviving entries by priority ascending with a documented,
// stable, lexical tie-break for a same-priority collision (never left to
// std::sort's own unspecified-for-equal-keys behavior).
//
// editor-core-separation-9 campaign, PHASE2 - queries `_v3` FIRST (the
// recommended path, Locked Product Decision #1), falling back to `_v2` only
// if a module does not implement `_v3`. A module declaring BOTH capabilities
// is a plugin-author error - loud GTE_LOG_WARNING, `_v3` wins (mirrors this
// codebase's general "loud, never silent" collision discipline).
void RenderFeatureCompositor::OnPluginsLoaded(const std::vector<IPluginModule*>& modules)
{
    for (IPluginModule* module : modules) {
        auto* v3Feature =
            static_cast<IRenderFeatureModule_v3*>(module->QueryCapability(kIRenderFeatureModule_v3_Name));
        auto* v2Feature =
            static_cast<IRenderFeatureModule_v2*>(module->QueryCapability(kIRenderFeatureModule_v2_Name));

        if (v3Feature == nullptr && v2Feature == nullptr) {
            continue;
        }

        if (v3Feature != nullptr && v2Feature != nullptr) {
            GtePluginModuleInfo info;
            module->GetModuleInfo(info);
            GTE_LOG_WARNING("RenderFeatureCompositor",
                std::string(info.name) + " declared BOTH IRenderFeatureModule_v3 and IRenderFeatureModule_v2 - "
                "a plugin must implement exactly one. Using _v3 and ignoring _v2 for this module.");
        }

        Entry entry;
        if (v3Feature != nullptr) {
            entry.moduleV3 = v3Feature;
            entry.descriptor = v3Feature->GetRenderFeatureDescriptor();
        } else {
            entry.moduleV2 = v2Feature;
            entry.descriptor = v2Feature->GetRenderFeatureDescriptor();
        }

        if (entry.descriptor.stage == RenderFeatureStage::PreOpaque
            || entry.descriptor.stage == RenderFeatureStage::PostOpaque
            || entry.descriptor.stage == RenderFeatureStage::PostTransparent) {
            GTE_LOG_WARNING("RenderFeatureCompositor",
                std::string(entry.descriptor.name) + " declared a RenderFeatureStage that is not wired in this "
                "engine build - this feature will not run any frame. See "
                "task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.");
            continue;
        }

        if (entry.descriptor.stage == RenderFeatureStage::PreUI) {
            m_preUi.push_back(entry);
        } else {
            m_postComposite.push_back(entry);
        }
    }

    SortAndDetectCollisionsInStage(m_postComposite, "PostComposite");
    SortAndDetectCollisionsInStage(m_preUi, "PreUI");

    // Populate the name pool for every surviving entry now, for BOTH known
    // views ("Game"/"Scene" - confirmed the only two RenderViewId::Named()
    // values anywhere in this engine, PHASE4's own Step 2 evidence) - the set
    // of loaded plugins AND the set of views are both fixed for the
    // process's entire remaining lifetime, never re-interned per frame.
    static constexpr const char* kViewNames[] = { "Game", "Scene" };
    for (const char* viewName : kViewNames) {
        m_namePool.SeedName(viewName);
        m_namePool.SeedCopyPassName(viewName);
        for (const Entry& entry : m_postComposite) {
            m_namePool.PrivateName(entry.descriptor.name, viewName);
            m_namePool.AccumName(entry.descriptor.name, viewName);
            m_namePool.BlendPassName(entry.descriptor.name, viewName);
        }
        for (const Entry& entry : m_preUi) {
            m_namePool.PrivateName(entry.descriptor.name, viewName);
            m_namePool.AccumName(entry.descriptor.name, viewName);
            m_namePool.BlendPassName(entry.descriptor.name, viewName);
        }
    }
}

// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - see this method's own doc
// comment (RenderFeatureCompositor.h) for the full contract. Walks the exact
// same combined (m_postComposite, then m_preUi) order
// ContributeRenderGraphPasses() itself uses, so the panel's displayed order
// always matches the REAL execution order this frame.
std::vector<RenderFeatureDebugEntry> RenderFeatureCompositor::DebugSnapshot() const
{
    std::vector<RenderFeatureDebugEntry> snapshot;
    snapshot.reserve(m_postComposite.size() + m_preUi.size());

    auto appendStage = [&snapshot](const std::vector<Entry>& entries) {
        for (const Entry& entry : entries) {
            RenderFeatureDebugEntry debugEntry;
            debugEntry.name = entry.descriptor.name;
            debugEntry.stage = ToString(entry.descriptor.stage);
            debugEntry.priority = entry.descriptor.priority;
            debugEntry.blendMode = ToString(entry.descriptor.blendMode);
            debugEntry.enabled = entry.enabledOverride;
            // editor-core-separation-9 campaign, PHASE4 - see
            // RenderFeatureDebugEntry.h's own doc comment (isV3) for the
            // full "why".
            debugEntry.isV3 = (entry.moduleV3 != nullptr);
            snapshot.push_back(std::move(debugEntry));
        }
    };

    appendStage(m_postComposite);
    appendStage(m_preUi);

    return snapshot;
}

void RenderFeatureCompositor::EnsureTextureSized(
    std::optional<RenderTexture>& texture, const char* internedName, VkExtent2D extent)
{
    const int width = extent.width > 0 ? static_cast<int>(extent.width) : 1;
    const int height = extent.height > 0 ? static_cast<int>(extent.height) : 1;

    if (!texture.has_value()) {
        texture.emplace(m_renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, internedName,
            /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true));
        return;
    }

    const VkExtent2D currentExtent = texture->Extent();
    if (currentExtent.width != extent.width || currentExtent.height != extent.height) {
        // Resizes are rare/user-driven (dragging the Game/Scene panel's
        // border) - a full device stall here is the simplest correct thing,
        // mirroring AtmosphereLutRenderer's own identical discipline.
        vkDeviceWaitIdle(m_device);
        texture->Resize(width, height);
    }
}

RenderFeatureCompositor::PrivateTargetState& RenderFeatureCompositor::EnsurePrivateTargetState(
    const char* internedName, VkExtent2D extent)
{
    const auto existing = m_privateTargetStates.find(internedName);
    if (existing != m_privateTargetStates.end()) {
        EnsureTextureSized(existing->second.texture, internedName, extent);
        return existing->second;
    }

    PrivateTargetState state;
    state.opsDescriptorSet =
        ComputeDescriptorSet(m_renderer.AllocateComputeDescriptorSet(m_operationRegistry.OpsDescriptorSetLayout()));
    EnsureTextureSized(state.texture, internedName, extent);
    const auto inserted = m_privateTargetStates.emplace(internedName, std::move(state));
    return inserted.first->second;
}

RenderFeatureCompositor::BlendStageState& RenderFeatureCompositor::EnsureBlendStageDescriptorOnly(
    const char* internedName)
{
    const auto existing = m_blendStageStates.find(internedName);
    if (existing != m_blendStageStates.end()) {
        return existing->second;
    }

    BlendStageState state;
    state.blendDescriptorSet = ComputeDescriptorSet(
        m_renderer.AllocateComputeDescriptorSet(m_operationRegistry.BlendDescriptorSetLayout()));
    const auto inserted = m_blendStageStates.emplace(internedName, std::move(state));
    return inserted.first->second;
}

RenderFeatureCompositor::BlendStageState& RenderFeatureCompositor::EnsureBlendStageState(
    const char* internedName, VkExtent2D extent)
{
    BlendStageState& state = EnsureBlendStageDescriptorOnly(internedName);
    EnsureTextureSized(state.texture, internedName, extent);
    return state;
}

// editor-core-separation-9 campaign, PHASE2 - see this method's own doc
// comment (RenderFeatureCompositor.h) for the full, load-bearing reasoning.
ComputeDescriptorSet& RenderFeatureCompositor::EnsureV3OpDescriptorSet(
    const std::string& key, VkDescriptorSetLayout layout)
{
    const auto existing = m_v3OpDescriptorSets.find(key);
    if (existing != m_v3OpDescriptorSets.end()) {
        return existing->second;
    }

    ComputeDescriptorSet descriptorSet(m_renderer.AllocateComputeDescriptorSet(layout));
    const auto inserted = m_v3OpDescriptorSets.emplace(key, std::move(descriptorSet));
    return inserted.first->second;
}

void RenderFeatureCompositor::DispatchOps(rg::RenderGraphBuilder& builder, rg::TextureHandle privateTarget,
    const char* stateKey, const char* debugName, const RenderFeatureOpsPushConstants& pushConstants)
{
    const auto it = m_privateTargetStates.find(stateKey);
    // Must already exist - ContributeRenderGraphPasses() creates/resizes it
    // before constructing the PluginRenderPassBuilderAdapter_v2 that calls
    // this - a missing entry here is a programmer error, not a
    // runtime-recoverable one.
    assert(it != m_privateTargetStates.end()
        && "RenderFeatureCompositor::DispatchOps: stateKey was never created by ContributeRenderGraphPasses() this "
           "frame");
    if (it == m_privateTargetStates.end()) {
        return;
    }
    PrivateTargetState& state = it->second;
    const VkExtent2D extent = state.texture->Extent();

    builder.AddRenderPass(debugName, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [privateTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(privateTarget, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &state, privateTarget, pushConstants, extent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(privateTarget);
            state.opsDescriptorSet.Rewrite(
                m_device, std::vector<ComputeDescriptorWrite>{ ComputeDescriptorWrite::StorageImage(0, dest.view) });

            RenderFeatureOpsPushConstants localPushConstants = pushConstants;
            const Extent3D groupCounts =
                ComputeGroupCount3D(Extent3D{ extent.width, extent.height, 1 }, Extent3D{ 16, 16, 1 });

            m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            m_renderer.Dispatch(m_operationRegistry.OpsPipeline(), state.opsDescriptorSet.Native(),
                &localPushConstants, sizeof(localPushConstants), groupCounts.width, groupCounts.height,
                groupCounts.depth);
            m_renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
}

void RenderFeatureCompositor::DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn,
    VkSampler dstInSampler, rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination,
    BlendStageState& state, const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode)
{
    builder.AddRenderPass(debugName, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [dstIn, srcIn, destination](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(dstIn, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(srcIn, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(destination, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &state, dstIn, dstInSampler, srcIn, srcInSampler, destination, extent, blendMode](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dstResolved = ctx.resolveTexture(dstIn);
            const rg::PassContext::ResolvedTexture srcResolved = ctx.resolveTexture(srcIn);
            const rg::PassContext::ResolvedTexture destResolved = ctx.resolveTexture(destination);

            state.blendDescriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, dstResolved.view, dstInSampler),
                    ComputeDescriptorWrite::CombinedImageSampler(1, srcResolved.view, srcInSampler),
                    ComputeDescriptorWrite::StorageImage(2, destResolved.view),
                });

            RenderFeatureBlendPushConstants pushConstants;
            pushConstants.blendModeAndPad[0] = static_cast<float>(static_cast<std::uint32_t>(blendMode));

            const Extent3D groupCounts =
                ComputeGroupCount3D(Extent3D{ extent.width, extent.height, 1 }, Extent3D{ 16, 16, 1 });

            m_renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            m_renderer.Dispatch(m_operationRegistry.BlendPipeline(), state.blendDescriptorSet.Native(),
                &pushConstants, sizeof(pushConstants), groupCounts.width, groupCounts.height, groupCounts.depth);
            m_renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
}

// editor-core-separation-6 campaign, PHASE4/PHASE5 - the real end-to-end
// pipeline: seed the chain (closing the same-physical-image read+write
// hazard for the N == 1 case), then walk every entry in this view's
// combined (PostComposite, then PreUI) list, giving each its own private
// target to draw into and its own dedicated blend dispatch (real,
// multi-mode RenderFeatureBlend.comp as of PHASE5) into either the next
// accumulator or (for the LAST entry) directly into the view's own real,
// final handle (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #10).
//
// editor-core-separation-9 campaign, PHASE2 - the per-entry loop now
// branches on which ABI version this entry implements: a `moduleV3` entry
// builds a PluginRenderPassBuilderAdapter_v3 (new); a `moduleV2` entry keeps
// building a PluginRenderPassBuilderAdapter_v2 (byte-for-byte unchanged) -
// everything AFTER this branch (the DispatchBlend() call reading
// `privateTarget`) is completely unchanged either way, since both adapters
// ultimately fill the SAME EnsurePrivateTargetState()-provided private
// RenderTexture.
void RenderFeatureCompositor::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&)
{
    // editor-core-separation-9 campaign, PHASE4
    // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.1) - a fresh,
    // empty blackboard every call (mirrors rg::RenderPassBlackboard's own
    // per-frame lifetime exactly) - cleared FIRST, before combinedList
    // construction, so a plugin can never see a stale value a DIFFERENT
    // view's own earlier-this-frame call published.
    m_blackboard.clear();

    std::vector<Entry> combinedList;
    combinedList.reserve(m_postComposite.size() + m_preUi.size());
    combinedList.insert(combinedList.end(), m_postComposite.begin(), m_postComposite.end());
    combinedList.insert(combinedList.end(), m_preUi.begin(), m_preUi.end());

    // editor-core-separation-8 campaign, PHASE2 - a host-disabled plugin
    // render feature is skipped entirely from this frame's compositing chain
    // (never contributes a pass, never consumes a private/blend target this
    // frame) - the entry itself is never removed from m_postComposite/m_preUi
    // (DebugSnapshot()/GET /render_graph keeps reporting it, disabled).
    combinedList.erase(std::remove_if(combinedList.begin(), combinedList.end(),
        [](const Entry& entry) { return !entry.enabledOverride; }), combinedList.end());
    if (combinedList.empty()) {
        return;
    }

    const std::optional<Core::PluginRenderFeatureTargetInfo> resolved = m_core.FindPluginRenderFeatureTarget(frame);
    if (!resolved.has_value()) {
        return;
    }

    const bool isGameView = (frame.currentView == rg::RenderViewId::Named("Game"));
    const std::string viewName = isGameView ? "Game" : "Scene";
    const VkExtent2D extent = resolved->extent;

    // Seed the chain - see this class's own header comment/PHASE4's own
    // Step 3.4 point 4 for exactly why: without this, `currentInput` and the
    // LAST entry's own `outputTarget` would be the SAME handle
    // (resolved->target) in the SAME dispatch whenever the combined list has
    // exactly one entry (N == 1) - a real GPU hazard (a compute pass
    // reading and writing the exact same storage image in one dispatch).
    // The seed dispatch is always a plain Replace copy, regardless of any
    // individual plugin's own declared blend mode (PHASE5_BLEND_MODE_COMPUTE_SHADER_AND_PREUI_STAGE.md's
    // own Step 3.2). This method calls m_operationRegistry.EnsureBuiltinsRegistered()
    // directly, here, before any per-frame descriptor-set/pipeline access
    // below - so both a _v2-only frame AND a _v3-only frame always have the
    // registry initialized before the per-entry loop runs; a _v3 plugin's
    // own Dispatch() call therefore never needs a separate
    // EnsureBuiltinsRegistered() call site of its own.
    //
    // editor-core-separation-9 campaign, PHASE2 - a REAL BUG found and fixed
    // during this phase's own required live smoke test (not merely "compiles
    // and doesn't crash"): EnsureOpsInitialized()/EnsureBlendPipelineInitialized()
    // used to be the ONLY methods that ever set this class's own `m_device`
    // member (used by EnsureTextureSized()'s vkDeviceWaitIdle() and every
    // DispatchOps()/DispatchBlend() Rewrite() call) - deleting them as part
    // of the pipeline-ownership migration (Step 3.2) silently left `m_device`
    // permanently VK_NULL_HANDLE, a real, confirmed ACCESS VIOLATION crash
    // (0xC0000005) the very first time this method actually did GPU work.
    // Fixed by sourcing `m_device` from the registry's own already-resolved
    // VkDevice (GetDevice()) - guaranteed valid immediately after
    // EnsureBuiltinsRegistered() returns.
    m_operationRegistry.EnsureBuiltinsRegistered();
    m_device = m_operationRegistry.GetDevice();

    const char* seedName = m_namePool.SeedName(viewName);
    BlendStageState& seedState = EnsureBlendStageState(seedName, extent);
    const rg::TextureHandle seedHandle =
        frame.builder.ImportTexture(seedName, seedState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
    DispatchBlend(frame.builder, resolved->target, resolved->sampler, resolved->target, resolved->sampler,
        seedHandle, seedState, m_namePool.SeedCopyPassName(viewName), extent, RenderFeatureBlendMode::Replace);

    rg::TextureHandle currentInput = seedHandle;
    VkSampler currentInputSampler = seedState.texture->Sampler();

    for (std::size_t i = 0; i < combinedList.size(); ++i) {
        const Entry& entry = combinedList[i];
        const std::string pluginName = entry.descriptor.name;
        const bool isLast = (i + 1 == combinedList.size());

        const char* privateName = m_namePool.PrivateName(pluginName, viewName);
        PrivateTargetState& privateState = EnsurePrivateTargetState(privateName, extent);
        const rg::TextureHandle privateTarget =
            frame.builder.ImportTexture(privateName, privateState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        if (entry.moduleV3 != nullptr) {
            PluginRenderPassBuilderAdapter_v3 adapter(frame.builder, privateTarget, m_operationRegistry,
                m_blackboardAdapter, resolved->target, resolved->sampler, *this,
                pluginName + "_" + viewName + "_");
            entry.moduleV3->AddRenderGraphPasses(adapter);
        } else {
            PluginRenderPassBuilderAdapter_v2 adapter(frame.builder, privateTarget, *this, privateName);
            entry.moduleV2->AddRenderGraphPasses(adapter);
        }

        rg::TextureHandle outputTarget;
        BlendStageState* outputState = nullptr;
        if (isLast) {
            // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #10 - the
            // LAST entry's blend writes back into the SAME handle the
            // legacy `_v1` path already writes into.
            outputTarget = resolved->target;
            outputState = &EnsureBlendStageDescriptorOnly(m_namePool.AccumName(pluginName, viewName));
        } else {
            const char* accumName = m_namePool.AccumName(pluginName, viewName);
            outputState = &EnsureBlendStageState(accumName, extent);
            outputTarget = frame.builder.ImportTexture(accumName, outputState->texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
        }

        DispatchBlend(frame.builder, currentInput, currentInputSampler, privateTarget,
            privateState.texture->Sampler(), outputTarget, *outputState, m_namePool.BlendPassName(pluginName, viewName),
            extent, entry.descriptor.blendMode);

        currentInput = outputTarget;
        currentInputSampler = isLast ? VK_NULL_HANDLE : outputState->texture->Sampler();
    }

    frame.finalTextureOutputs.push_back(resolved->target);
}

} // namespace gte
