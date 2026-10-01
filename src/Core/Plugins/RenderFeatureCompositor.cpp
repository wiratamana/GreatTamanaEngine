#include "RenderFeatureCompositor.h"

// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// #include "PluginRenderPassBuilderAdapter_v2.h"/"PluginRenderPassBuilderAdapter_v3.h"
// removed - both classes are deleted outright this phase (ABI-only). A
// direct #include of the real IPluginModule.h is now needed here (this
// .cpp's own OnPluginsLoaded() calls module->QueryCapability()/
// GetModuleInfo() directly) - it used to reach gte_core transitively
// through the two deleted adapter headers above; IPluginCapabilityOrchestrator.h
// only forward-declares IPluginModule.
#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../Core.h"
#include "../Logging.h"

#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "../../../plugins/gte_plugin_abi/IPluginRenderPassBuilder_v3.h"

#include "../../Renderer/ComputeDispatch.h"

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

RenderFeatureCompositor::RenderFeatureCompositor(Core& core, Renderer& renderer)
    : m_core(core)
    , m_renderer(renderer)
    , m_blackboardAdapter(*this)
{
    // editor-core-separation-23 campaign, PHASE2 - the bounded, reusable
    // GPU-state slot free-list, fully populated at construction time. See
    // this class's own kMaxConcurrentProjectRenderFeatures/
    // m_freeProjectFeatureSlots doc comments (RenderFeatureCompositor.h) for
    // the full reasoning.
    for (int i = 0; i < kMaxConcurrentProjectRenderFeatures; ++i) {
        m_freeProjectFeatureSlots.push_back(i);
    }
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

// editor-core-separation-23 campaign, PHASE5
// (PHASE5_ORDERING_SAFETY_NET_AND_LIFETIME_CONFIRMATION.md, Step 3.2) - see
// this method's own doc comment (RenderFeatureCompositor.h) for the full
// "why". A cheap, debug-build-only check - assert() is a true no-op in a
// release (NDEBUG) build.
void RenderFeatureCompositor::AssertCalledFromMainThread() const
{
    assert(std::this_thread::get_id() == m_mainThreadId
        && "RenderFeatureCompositor::RegisterProjectFeature()/UnregisterProjectFeature() are documented "
           "main-thread-only contracts (exactly like ContributeRenderGraphPasses() itself) - this call came from a "
           "different thread.");
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

// editor-core-separation-23 campaign, PHASE2
// (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.3) - see this
// method's own doc comment (RenderFeatureCompositor.h) for the full contract.
bool RenderFeatureCompositor::RegisterProjectFeature(
    const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback)
{
    AssertCalledFromMainThread();

    if (m_freeProjectFeatureSlots.empty()) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterProjectFeature('") + descriptor.name + "') refused - every one of the "
            + std::to_string(kMaxConcurrentProjectRenderFeatures)
            + " kMaxConcurrentProjectRenderFeatures slots is currently claimed by another still-registered "
              "project render feature.");
        return false;
    }

    if (FindEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterProjectFeature('") + descriptor.name
            + "') refused - a render feature with this name is already registered.");
        return false;
    }

    const int claimedSlot = m_freeProjectFeatureSlots.back();
    m_freeProjectFeatureSlots.pop_back();

    Entry entry;
    entry.projectCallback = std::move(callback);
    entry.descriptor = descriptor;
    entry.projectFeatureSlot = claimedSlot;

    if (entry.descriptor.stage == RenderFeatureStage::PreOpaque
        || entry.descriptor.stage == RenderFeatureStage::PostOpaque
        || entry.descriptor.stage == RenderFeatureStage::PostTransparent) {
        // Mirrors OnPluginsLoaded()'s own identical refusal block - an
        // unwired stage never claims a permanent slot; release it back
        // before returning.
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string(entry.descriptor.name) + " declared a RenderFeatureStage that is not wired in this "
            "engine build - this feature will not run any frame. See "
            "task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.");
        m_freeProjectFeatureSlots.push_back(claimedSlot);
        return false;
    }

    const bool isPreUi = (entry.descriptor.stage == RenderFeatureStage::PreUI);
    std::vector<Entry>& targetStage = isPreUi ? m_preUi : m_postComposite;
    targetStage.push_back(std::move(entry));
    SortAndDetectCollisionsInStage(targetStage, isPreUi ? "PreUI" : "PostComposite");

    // Re-seed the name pool for the newly-added entry, for BOTH known views -
    // mirroring OnPluginsLoaded()'s own trailing loop, with the ONE
    // deliberate difference: the string fed into PrivateName()/AccumName()/
    // BlendPassName() is the slot-derived gpuStateKey, never descriptor.name
    // (PHASE0_MASTER_STRATEGY.md's Locked Decision #4).
    const std::string gpuStateKey = "ProjectFeatureSlot" + std::to_string(claimedSlot);
    static constexpr const char* kViewNames[] = { "Game", "Scene" };
    for (const char* viewName : kViewNames) {
        m_namePool.PrivateName(gpuStateKey, viewName);
        m_namePool.AccumName(gpuStateKey, viewName);
        m_namePool.BlendPassName(gpuStateKey, viewName);
    }

    GTE_LOG_INFO("RenderFeatureCompositor",
        std::string("RegisterProjectFeature('") + descriptor.name + "') succeeded - claimed GPU-state slot "
        + std::to_string(claimedSlot) + ".");
    return true;
}

// editor-core-separation-23 campaign, PHASE2 - see this method's own doc
// comment (RenderFeatureCompositor.h) for the full contract.
bool RenderFeatureCompositor::UnregisterProjectFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    Entry* entry = FindEntryByName(name);
    if (entry == nullptr) {
        return false;
    }

    if (!entry->projectCallback) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("UnregisterProjectFeature('") + name + "') refused - this entry is not a Project "
            "Assembly render feature (it is a loaded plugin's own moduleV2/moduleV3 entry).");
        return false;
    }

    // Release the slot back to the free list BEFORE erasing the entry
    // (erase() invalidates `entry` itself - never dereferenced again after
    // this point). Deliberately do NOT touch m_privateTargetStates/
    // m_blendStageStates entries for this slot's own interned names - see
    // this method's own header doc comment for why.
    m_freeProjectFeatureSlots.push_back(entry->projectFeatureSlot);

    const std::string nameStr = name;
    const bool isPostComposite =
        std::find_if(m_postComposite.begin(), m_postComposite.end(),
            [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; })
        != m_postComposite.end();
    if (isPostComposite) {
        m_postComposite.erase(std::remove_if(m_postComposite.begin(), m_postComposite.end(),
                                   [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; }),
            m_postComposite.end());
    } else {
        m_preUi.erase(std::remove_if(m_preUi.begin(), m_preUi.end(),
                           [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; }),
            m_preUi.end());
    }

    GTE_LOG_INFO("RenderFeatureCompositor",
        std::string("UnregisterProjectFeature('") + name + "') succeeded - released GPU-state slot back to the "
        "free list.");
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
            // editor-core-separation-23 campaign, PHASE2 - see
            // RenderFeatureDebugEntry.h's own doc comment (isProjectFeature)
            // for the full "why" - mirrors isV3's own exact precedent.
            debugEntry.isProjectFeature = static_cast<bool>(entry.projectCallback);
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

    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // `state.opsDescriptorSet = ComputeDescriptorSet(...)` removed - that
    // field is deleted from PrivateTargetState (it was only ever consumed by
    // the now-deleted DispatchOps()).
    PrivateTargetState state;
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

    // better-render-pass-2 campaign, PHASE1 - must run before the
    // AllocateComputeDescriptorSet() call just below, since that call reads
    // m_blendDescriptorSetLayout, which this lazily builds on first use.
    EnsureBlendPipelineInitialized();

    BlendStageState state;
    state.blendDescriptorSet = ComputeDescriptorSet(m_renderer.AllocateComputeDescriptorSet(m_blendDescriptorSetLayout));
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

// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// DispatchOps() (the `_v2` uber-shader dispatch, RenderFeatureOps.comp)
// removed outright - its only real caller, PluginRenderPassBuilderAdapter_v2,
// is deleted this same phase, and its own RenderFeatureOpsPushConstants
// parameter type lived in the now-deleted PluginRenderOperationRegistry.h.

// better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md) -
// the shared blend pipeline (RenderFeatureBlend.comp) construction, RELOCATED
// here VERBATIM from PluginRenderOperationRegistry::EnsureBuiltinsRegistered() -
// same descriptor bindings (binding 0/1 = CombinedImageSampler for dstIn/srcIn,
// binding 2 = StorageImage for destination), same real SPIR-V-reflection-based
// CreateComputePipeline()/ReflectedDescriptorSetLayout() path. Idempotent -
// safe to call more than once per frame, from more than one call site (see
// this method's own doc comment, RenderFeatureCompositor.h).
void RenderFeatureCompositor::EnsureBlendPipelineInitialized()
{
    if (m_blendPipeline.has_value()) {
        return;
    }
    m_blendPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureBlend.comp.spv"));
    m_blendDescriptorSetLayout = m_blendPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

void RenderFeatureCompositor::DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn,
    VkSampler dstInSampler, rg::TextureHandle srcIn, VkSampler srcInSampler, rg::TextureHandle destination,
    BlendStageState& state, const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode)
{
    // better-render-pass-2 campaign, PHASE1 - ensures m_blendPipeline is ready
    // even on the rare path where this is reached without
    // EnsureBlendStageDescriptorOnly() having run first this process
    // lifetime (defense-in-depth; idempotent, cheap no-op after the first
    // real call from either call site).
    EnsureBlendPipelineInitialized();
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
            m_renderer.Dispatch(*m_blendPipeline, state.blendDescriptorSet.Native(),
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
    // individual plugin's own declared blend mode. `m_device` is resolved
    // directly from m_renderer.GetVulkanContextInfo().device below, before
    // any per-frame descriptor-set/pipeline access.
    //
    // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
    // this used to be sourced via m_operationRegistry.EnsureBuiltinsRegistered()/
    // GetDevice() (editor-core-separation-9 campaign, PHASE2's own fix for a
    // real, confirmed ACCESS VIOLATION crash from an uninitialized m_device) -
    // PluginRenderOperationRegistry is deleted outright this phase (ABI-only),
    // so this now reads the SAME real VkDevice directly off Renderer instead.
    m_device = m_renderer.GetVulkanContextInfo().device;

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
        const std::string pluginName = entry.descriptor.name; // unchanged - still used for FindEntryByName()/logs/adapter construction.
        // editor-core-separation-23 campaign, PHASE2
        // (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.4) - a
        // Project Assembly's own render-feature GPU state is NEVER keyed by
        // its human-typed descriptor.name (PHASE0_MASTER_STRATEGY.md's
        // Locked Decision #4) - it uses a bounded, slot-derived key instead.
        // For every moduleV2/moduleV3 entry this is a complete no-op:
        // gpuStateKey == pluginName exactly, byte-for-byte, since
        // entry.projectCallback is always unset for them.
        const std::string gpuStateKey = (entry.projectCallback)
            ? ("ProjectFeatureSlot" + std::to_string(entry.projectFeatureSlot))
            : pluginName;
        const bool isLast = (i + 1 == combinedList.size());

        const char* privateName = m_namePool.PrivateName(gpuStateKey, viewName);
        PrivateTargetState& privateState = EnsurePrivateTargetState(privateName, extent);
        const rg::TextureHandle privateTarget =
            frame.builder.ImportTexture(privateName, privateState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        // better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
        // the `entry.moduleV3`/`entry.moduleV2` branches
        // (PluginRenderPassBuilderAdapter_v3/_v2, both deleted outright this
        // phase, ABI-only) are removed - neither field is ever set anymore
        // (nothing can load a plugin since PHASE2 removed the one call site
        // that ever invoked Core::LoadPlugins()), so this was always a
        // no-op dead branch at runtime; only the Project Assembly path
        // survives, completely unchanged.
        if (entry.projectCallback) {
            entry.projectCallback(frame.builder, privateTarget, extent);
        }

        rg::TextureHandle outputTarget;
        BlendStageState* outputState = nullptr;
        if (isLast) {
            // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #10 - the
            // LAST entry's blend writes back into the SAME handle the
            // legacy `_v1` path already writes into.
            outputTarget = resolved->target;
            outputState = &EnsureBlendStageDescriptorOnly(m_namePool.AccumName(gpuStateKey, viewName));
        } else {
            const char* accumName = m_namePool.AccumName(gpuStateKey, viewName);
            outputState = &EnsureBlendStageState(accumName, extent);
            outputTarget = frame.builder.ImportTexture(accumName, outputState->texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
        }

        DispatchBlend(frame.builder, currentInput, currentInputSampler, privateTarget,
            privateState.texture->Sampler(), outputTarget, *outputState, m_namePool.BlendPassName(gpuStateKey, viewName),
            extent, entry.descriptor.blendMode);

        currentInput = outputTarget;
        currentInputSampler = isLast ? VK_NULL_HANDLE : outputState->texture->Sampler();
    }

    frame.finalTextureOutputs.push_back(resolved->target);
}

} // namespace gte
