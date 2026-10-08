#include "RenderFeatureCompositor.h"

// better-render-pass-2 campaign, PHASE3 (PHASE3_DELETE_ABI_HOST_CODE.md) -
// #include "PluginRenderPassBuilderAdapter_v2.h"/"PluginRenderPassBuilderAdapter_v3.h"
// removed - both classes are deleted outright this phase (ABI-only).
//
// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// the direct #includes of "../../../plugins/gte_plugin_abi/IPluginModule.h"/
// "GtePluginModuleInfo.h"/"IPluginRenderPassBuilder_v3.h" this file used to
// need (OnPluginsLoaded()'s own plugin-discovery scan body, the
// BlackboardAdapter/IPluginBlackboard implementation) are removed outright,
// alongside the code that needed them - see OnPluginsLoaded()'s own new,
// empty body below and the deleted BlackboardAdapter class
// (RenderFeatureCompositor.h).
#include "RenderFeatureCameraData.h"
#include "PostProcessingPassTags.h"
#include "../Core.h"
#include "../Logging.h"

#include "../../Renderer/ComputeDispatch.h"
#include "../../Renderer/RenderGraph/RenderPassGroupRegistry.h"

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

} // namespace

RenderFeatureCompositor::RenderFeatureCompositor(Core& core, Renderer& renderer)
    : m_core(core)
    , m_renderer(renderer)
{
    // Render Graph panel grouping headings for this compositor's own
    // blend-chain compute dispatches - idempotent, safe to call every
    // construction.
    rg::RegisterPassGroupLabel(kPostProcessingPassTag, "Post Processing");
    rg::RegisterPassGroupLabel(kProjectFeatureGroupTag, "Project Features");

    // editor-core-separation-23 campaign, PHASE2 - the bounded, reusable
    // GPU-state slot free-list, fully populated at construction time. See
    // this class's own kMaxConcurrentProjectRenderFeatures/
    // m_freeProjectFeatureSlots doc comments (RenderFeatureCompositor.h) for
    // the full reasoning.
    for (int i = 0; i < kMaxConcurrentProjectRenderFeatures; ++i) {
        m_freeProjectFeatureSlots.push_back(i);
    }
}

// editor-core-separation-8 campaign, PHASE2 - extracted VERBATIM from the
// former inline `sortAndDetectCollisions` lambda (zero behavior change), so
// SetFeaturePriority() can reuse the exact same sort+collision-tie-break
// logic for a single re-sort after a live priority change, without
// duplicating it.
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
                    "Assign each feature a distinct priority to remove this warning.");
            }
            std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                entries.begin() + static_cast<std::ptrdiff_t>(j) + 1, [](const Entry& a, const Entry& b) {
                    return std::strcmp(a.descriptor.name, b.descriptor.name) < 0;
                });
        }
        i = j + 1;
    }
}

// better-render-pass-5 effort, BLOCK 3, PHASE2 - PreOpaque sibling of
// SortAndDetectCollisionsInStage() immediately above - identical
// algorithm, against PreOpaqueEntry's own name/priority fields directly
// (no nested .descriptor) and a fixed "PreOpaque" stage name in every
// log line (there is only ever one PreOpaque list, unlike
// SortAndDetectCollisionsInStage()'s own two callers for two different
// stage names).
void RenderFeatureCompositor::SortAndDetectCollisionsInPreOpaqueList(std::vector<PreOpaqueEntry>& entries)
{
    std::stable_sort(entries.begin(), entries.end(),
        [](const PreOpaqueEntry& a, const PreOpaqueEntry& b) { return a.priority < b.priority; });

    std::size_t i = 0;
    while (i < entries.size()) {
        std::size_t j = i;
        while (j + 1 < entries.size() && entries[j + 1].priority == entries[i].priority) {
            ++j;
        }
        if (j > i) {
            for (std::size_t k = i; k < j; ++k) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    entries[k].name + " and " + entries[k + 1].name + " both declared priority "
                    + std::to_string(entries[k].priority) + " in stage PreOpaque - this is ambiguous; falling "
                    "back to a stable, lexical name tie-break. Assign each feature a distinct priority to remove "
                    "this warning.");
            }
            std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                entries.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                [](const PreOpaqueEntry& a, const PreOpaqueEntry& b) { return a.name < b.name; });
        }
        i = j + 1;
    }
}

void RenderFeatureCompositor::SortAndDetectCollisionsInPostOpaqueList(std::vector<PostOpaqueEntry>& entries)
{
    std::stable_sort(entries.begin(), entries.end(),
        [](const PostOpaqueEntry& a, const PostOpaqueEntry& b) { return a.priority < b.priority; });

    std::size_t i = 0;
    while (i < entries.size()) {
        std::size_t j = i;
        while (j + 1 < entries.size() && entries[j + 1].priority == entries[i].priority) {
            ++j;
        }
        if (j > i) {
            for (std::size_t k = i; k < j; ++k) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    entries[k].name + " and " + entries[k + 1].name + " both declared priority "
                    + std::to_string(entries[k].priority) + " in stage PostOpaque - this is ambiguous; falling "
                    "back to a stable, lexical name tie-break. Assign each feature a distinct priority to remove "
                    "this warning.");
            }
            std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                entries.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                [](const PostOpaqueEntry& a, const PostOpaqueEntry& b) { return a.name < b.name; });
        }
        i = j + 1;
    }
}

void RenderFeatureCompositor::SortAndDetectCollisionsInPostTransparentList(std::vector<PostTransparentEntry>& entries)
{
    std::stable_sort(entries.begin(), entries.end(),
        [](const PostTransparentEntry& a, const PostTransparentEntry& b) { return a.priority < b.priority; });

    std::size_t i = 0;
    while (i < entries.size()) {
        std::size_t j = i;
        while (j + 1 < entries.size() && entries[j + 1].priority == entries[i].priority) {
            ++j;
        }
        if (j > i) {
            for (std::size_t k = i; k < j; ++k) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    entries[k].name + " and " + entries[k + 1].name + " both declared priority "
                    + std::to_string(entries[k].priority) + " in stage PostTransparent - this is ambiguous; falling "
                    "back to a stable, lexical name tie-break. Assign each feature a distinct priority to remove "
                    "this warning.");
            }
            std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                entries.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                [](const PostTransparentEntry& a, const PostTransparentEntry& b) { return a.name < b.name; });
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

// One shared combined gate for "is this feature really running": reads
// BOTH the host-side override AND (only when the name is independently
// known there too) the toggle registry's own flag - never auto-creates a
// toggle-registry entry as a side effect of reading it.
bool RenderFeatureCompositor::IsEffectivelyEnabled(const std::string& name, bool enabledOverride) const
{
    if (!enabledOverride) {
        return false;
    }
    const rg::RenderPassToggleRegistry& toggles = m_core.GetRenderPassToggleRegistryMutable();
    return !toggles.HasEntry(name) || toggles.IsEnabled(name);
}

// better-render-pass-5 effort, BLOCK 3, PHASE2 - see this method's own
// doc comment (RenderFeatureCompositor.h) for the full contract.
RenderFeatureCompositor::PreOpaqueEntry* RenderFeatureCompositor::FindPreOpaqueEntryByName(const std::string& name)
{
    for (PreOpaqueEntry& entry : m_preOpaque) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

RenderFeatureCompositor::PostOpaqueEntry* RenderFeatureCompositor::FindPostOpaqueEntryByName(const std::string& name)
{
    for (PostOpaqueEntry& entry : m_postOpaque) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

RenderFeatureCompositor::PostTransparentEntry* RenderFeatureCompositor::FindPostTransparentEntryByName(
    const std::string& name)
{
    for (PostTransparentEntry& entry : m_postTransparent) {
        if (name == entry.name) {
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

// Host-side enable/disable override. Single exit point - the toggle
// registry mirror-write below happens exactly once, and only when
// HasEntry() already says `name` lives there too (never upserts a ghost
// pass-tree row for an ordinary render feature unrelated to the registry).
bool RenderFeatureCompositor::SetFeatureEnabled(const std::string& name, bool enabled)
{
    bool found = false;
    if (Entry* entry = FindEntryByName(name)) {
        entry->enabledOverride = enabled;
        found = true;
    } else if (PreOpaqueEntry* preOpaqueEntry = FindPreOpaqueEntryByName(name)) {
        preOpaqueEntry->enabledOverride = enabled;
        found = true;
    } else if (PostOpaqueEntry* postOpaqueEntry = FindPostOpaqueEntryByName(name)) {
        postOpaqueEntry->enabledOverride = enabled;
        found = true;
    } else if (PostTransparentEntry* postTransparentEntry = FindPostTransparentEntryByName(name)) {
        postTransparentEntry->enabledOverride = enabled;
        found = true;
    }
    if (!found) {
        return false;
    }

    rg::RenderPassToggleRegistry& toggles = m_core.GetRenderPassToggleRegistryMutable();
    if (toggles.HasEntry(name)) {
        toggles.SetEnabled(name, enabled);
    }
    return true;
}

// editor-core-separation-8 campaign, PHASE2 - host-side LIVE priority
// override. See RenderFeatureCompositor.h's own doc comment for the full
// contract.
bool RenderFeatureCompositor::SetFeaturePriority(const std::string& name, std::int32_t priority)
{
    if (Entry* entry = FindEntryByName(name)) {
        entry->descriptor.priority = priority;

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

    // better-render-pass-5 effort, BLOCK 3, PHASE2 - see
    // SetFeatureEnabled()'s own identical fallback comment above.
    if (PreOpaqueEntry* preOpaqueEntry = FindPreOpaqueEntryByName(name)) {
        preOpaqueEntry->priority = priority;
        SortAndDetectCollisionsInPreOpaqueList(m_preOpaque);
        return true;
    }

    if (PostOpaqueEntry* postOpaqueEntry = FindPostOpaqueEntryByName(name)) {
        postOpaqueEntry->priority = priority;
        SortAndDetectCollisionsInPostOpaqueList(m_postOpaque);
        return true;
    }
    if (PostTransparentEntry* postTransparentEntry = FindPostTransparentEntryByName(name)) {
        postTransparentEntry->priority = priority;
        SortAndDetectCollisionsInPostTransparentList(m_postTransparent);
        return true;
    }

    return false;
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

    // better-render-pass-5 effort, BLOCK 3, PHASE2 - PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #8 - the symmetric reverse of
    // RegisterPreOpaqueFeature()'s own new cross-namespace check above:
    // PreOpaque/PostComposite/PreUI feature names share one global
    // namespace.
    if (FindPreOpaqueEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterProjectFeature('") + descriptor.name
            + "') refused - a PreOpaque feature with this name is already registered (PreOpaque/PostComposite/"
            "PreUI feature names share one global namespace).");
        return false;
    }

    if (FindPostOpaqueEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterProjectFeature('") + descriptor.name
            + "') refused - a PostOpaque feature with this name is already registered (feature names share one "
            "global namespace across every stage).");
        return false;
    }
    if (FindPostTransparentEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterProjectFeature('") + descriptor.name
            + "') refused - a PostTransparent feature with this name is already registered (feature names share "
            "one global namespace across every stage).");
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
        // An unwired stage never claims a permanent slot; release it back
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
    // the string fed into PrivateName()/AccumName()/BlendPassName() is the
    // slot-derived gpuStateKey, never descriptor.name (PHASE0_MASTER_STRATEGY.md's
    // Locked Decision #4).
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
        // better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
        // this branch is now structurally unreachable (every surviving Entry
        // always has projectCallback set, since the moduleV2/moduleV3
        // alternative was removed) - kept as a defensive guard, never
        // removed, since FindEntryByName() returning a non-null Entry whose
        // projectCallback happens to be unset is still a real, checkable
        // invariant worth refusing loudly rather than assuming away.
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("UnregisterProjectFeature('") + name + "') refused - this entry is not a Project "
            "Assembly render feature.");
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

// better-render-pass-5 effort, BLOCK 3, PHASE2 - see this method's own
// doc comment (RenderFeatureCompositor.h) for the full contract.
bool RenderFeatureCompositor::RegisterPreOpaqueFeature(
    const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback)
{
    AssertCalledFromMainThread();

    // better-render-pass-5 effort, BLOCK 3, PHASE2 - PHASE0_MASTER_STRATEGY.md's
    // Locked Design Decision #8 (confirmed by explicit user decision,
    // Step 2.5 item 3): PreOpaque feature names share ONE GLOBAL
    // namespace with PostComposite/PreUI - check BOTH FindPreOpaqueEntryByName()
    // AND FindEntryByName() before accepting a new PreOpaque registration.
    if (FindPreOpaqueEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPreOpaqueFeature('" + name + "') refused - a PreOpaque feature with this name is already "
            "registered.");
        return false;
    }
    if (FindEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPreOpaqueFeature('" + name + "') refused - a PostComposite/PreUI feature with this name is "
            "already registered (PreOpaque/PostComposite/PreUI feature names share one global namespace).");
        return false;
    }
    if (FindPostOpaqueEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPreOpaqueFeature('" + name + "') refused - a PostOpaque feature with this name is already "
            "registered (feature names share one global namespace across every stage).");
        return false;
    }
    if (FindPostTransparentEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPreOpaqueFeature('" + name + "') refused - a PostTransparent feature with this name is "
            "already registered (feature names share one global namespace across every stage).");
        return false;
    }

    PreOpaqueEntry entry;
    entry.name = name;
    entry.priority = priority;
    entry.callback = std::move(callback);
    m_preOpaque.push_back(std::move(entry));
    SortAndDetectCollisionsInPreOpaqueList(m_preOpaque);

    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterPreOpaqueFeature('" + name + "') succeeded.");
    return true;
}

// better-render-pass-5 effort, BLOCK 3, PHASE2 - see this method's own
// doc comment (RenderFeatureCompositor.h) for the full contract.
bool RenderFeatureCompositor::UnregisterPreOpaqueFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    const std::size_t sizeBefore = m_preOpaque.size();
    m_preOpaque.erase(
        std::remove_if(m_preOpaque.begin(), m_preOpaque.end(),
            [&nameStr](const PreOpaqueEntry& e) { return nameStr == e.name; }),
        m_preOpaque.end());

    if (m_preOpaque.size() == sizeBefore) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterPreOpaqueFeature('" + nameStr + "') refused - no such PreOpaque feature is registered.");
        return false;
    }

    GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterPreOpaqueFeature('" + nameStr + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::RegisterPostOpaqueFeature(
    const std::string& name, std::int32_t priority, ProjectScenePassCallback callback)
{
    AssertCalledFromMainThread();

    if (FindPostOpaqueEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostOpaqueFeature('" + name + "') refused - a PostOpaque feature with this name is already "
            "registered.");
        return false;
    }
    if (FindPostTransparentEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostOpaqueFeature('" + name + "') refused - a PostTransparent feature with this name is "
            "already registered (feature names share one global namespace across every stage).");
        return false;
    }
    if (FindPreOpaqueEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostOpaqueFeature('" + name + "') refused - a PreOpaque feature with this name is already "
            "registered (feature names share one global namespace across every stage).");
        return false;
    }
    if (FindEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostOpaqueFeature('" + name + "') refused - a PostComposite/PreUI feature with this name is "
            "already registered (feature names share one global namespace across every stage).");
        return false;
    }

    PostOpaqueEntry entry;
    entry.name = name;
    entry.priority = priority;
    entry.callback = std::move(callback);
    m_postOpaque.push_back(std::move(entry));
    SortAndDetectCollisionsInPostOpaqueList(m_postOpaque);

    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterPostOpaqueFeature('" + name + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::UnregisterPostOpaqueFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    const std::size_t sizeBefore = m_postOpaque.size();
    m_postOpaque.erase(
        std::remove_if(m_postOpaque.begin(), m_postOpaque.end(),
            [&nameStr](const PostOpaqueEntry& e) { return nameStr == e.name; }),
        m_postOpaque.end());

    if (m_postOpaque.size() == sizeBefore) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterPostOpaqueFeature('" + nameStr + "') refused - no such PostOpaque feature is registered.");
        return false;
    }

    GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterPostOpaqueFeature('" + nameStr + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::RegisterPostTransparentFeature(
    const std::string& name, std::int32_t priority, ProjectScenePassCallback callback)
{
    AssertCalledFromMainThread();

    if (FindPostTransparentEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostTransparentFeature('" + name + "') refused - a PostTransparent feature with this name is "
            "already registered.");
        return false;
    }
    if (FindPostOpaqueEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostTransparentFeature('" + name + "') refused - a PostOpaque feature with this name is "
            "already registered (feature names share one global namespace across every stage).");
        return false;
    }
    if (FindPreOpaqueEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostTransparentFeature('" + name + "') refused - a PreOpaque feature with this name is "
            "already registered (feature names share one global namespace across every stage).");
        return false;
    }
    if (FindEntryByName(name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterPostTransparentFeature('" + name + "') refused - a PostComposite/PreUI feature with this name "
            "is already registered (feature names share one global namespace across every stage).");
        return false;
    }

    PostTransparentEntry entry;
    entry.name = name;
    entry.priority = priority;
    entry.callback = std::move(callback);
    m_postTransparent.push_back(std::move(entry));
    SortAndDetectCollisionsInPostTransparentList(m_postTransparent);

    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterPostTransparentFeature('" + name + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::UnregisterPostTransparentFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    const std::size_t sizeBefore = m_postTransparent.size();
    m_postTransparent.erase(
        std::remove_if(m_postTransparent.begin(), m_postTransparent.end(),
            [&nameStr](const PostTransparentEntry& e) { return nameStr == e.name; }),
        m_postTransparent.end());

    if (m_postTransparent.size() == sizeBefore) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterPostTransparentFeature('" + nameStr
            + "') refused - no such PostTransparent feature is registered.");
        return false;
    }

    GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterPostTransparentFeature('" + nameStr + "') succeeded.");
    return true;
}

// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// intentionally empty as of this campaign: `IPluginCapabilityOrchestrator`
// requires an override (its own OnPluginsLoaded() is a pure virtual with no
// default body - IPluginCapabilityOrchestrator.h), but nothing calls this
// anymore (Decision D2, PHASE0_MASTER_STRATEGY.md Section 2.4 - provably true
// since PHASE2 of this same campaign removed EditorHost.cpp's one-and-only
// Core::LoadPlugins() call site) - kept only to satisfy the pure-virtual
// contract. The former body (the `_v2`/`_v3` plugin-discovery scan, stage
// collision detection, and name-pool pre-warm loop) is deleted outright,
// along with the `Entry::moduleV2`/`moduleV3` fields it populated
// (RenderFeatureCompositor.h).
void RenderFeatureCompositor::OnPluginsLoaded(const std::vector<IPluginModule*>& /*modules*/)
{
}

// A read-only snapshot of this compositor's own REAL, resolved ordering
// decision - walks the exact same combined (m_postComposite, then m_preUi)
// order ContributeRenderGraphPasses() itself uses, so the panel's displayed
// order always matches the real execution order this frame. `enabled`
// reports the COMBINED gate (IsEffectivelyEnabled()) - never just the raw
// host override - so a dual-citizen name like "AtmosphereComposite" never
// shows "enabled" here while the toggle registry silently vetoes it.
std::vector<RenderFeatureDebugEntry> RenderFeatureCompositor::DebugSnapshot() const
{
    std::vector<RenderFeatureDebugEntry> snapshot;
    snapshot.reserve(m_postComposite.size() + m_preUi.size() + m_preOpaque.size() + m_postOpaque.size()
        + m_postTransparent.size());

    auto appendStage = [&snapshot, this](const std::vector<Entry>& entries) {
        for (const Entry& entry : entries) {
            RenderFeatureDebugEntry debugEntry;
            debugEntry.name = entry.descriptor.name;
            debugEntry.stage = ToString(entry.descriptor.stage);
            debugEntry.priority = entry.descriptor.priority;
            debugEntry.blendMode = ToString(entry.descriptor.blendMode);
            debugEntry.enabled = IsEffectivelyEnabled(entry.descriptor.name, entry.enabledOverride);
            debugEntry.isProjectFeature = static_cast<bool>(entry.projectCallback);
            snapshot.push_back(std::move(debugEntry));
        }
    };

    appendStage(m_postComposite);
    appendStage(m_preUi);

    // PreOpaque/PostOpaque/PostTransparent features live in separate
    // lists/types (no nested `.descriptor`, no real blendMode concept) so
    // each gets its own small, inline loop rather than the shared
    // `appendStage` lambda above.
    for (const PreOpaqueEntry& entry : m_preOpaque) {
        RenderFeatureDebugEntry debugEntry;
        debugEntry.name = entry.name;
        debugEntry.stage = "PreOpaque";
        debugEntry.priority = entry.priority;
        debugEntry.blendMode = "None";
        debugEntry.enabled = IsEffectivelyEnabled(entry.name, entry.enabledOverride);
        debugEntry.isProjectFeature = static_cast<bool>(entry.callback);
        snapshot.push_back(std::move(debugEntry));
    }

    for (const PostOpaqueEntry& entry : m_postOpaque) {
        RenderFeatureDebugEntry debugEntry;
        debugEntry.name = entry.name;
        debugEntry.stage = "PostOpaque";
        debugEntry.priority = entry.priority;
        debugEntry.blendMode = "None";
        debugEntry.enabled = IsEffectivelyEnabled(entry.name, entry.enabledOverride);
        debugEntry.isProjectFeature = static_cast<bool>(entry.callback);
        snapshot.push_back(std::move(debugEntry));
    }
    for (const PostTransparentEntry& entry : m_postTransparent) {
        RenderFeatureDebugEntry debugEntry;
        debugEntry.name = entry.name;
        debugEntry.stage = "PostTransparent";
        debugEntry.priority = entry.priority;
        debugEntry.blendMode = "None";
        debugEntry.enabled = IsEffectivelyEnabled(entry.name, entry.enabledOverride);
        debugEntry.isProjectFeature = static_cast<bool>(entry.callback);
        snapshot.push_back(std::move(debugEntry));
    }

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
    BlendStageState& state, const char* debugName, VkExtent2D extent, RenderFeatureBlendMode blendMode,
    rg::RenderPassTagMask tagMask)
{
    // Idempotent - safe to call from more than one call site per frame.
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
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything, tagMask);
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
// better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
// the per-entry loop below only ever has the entry.projectCallback branch now
// (PHASE3 of that same campaign already removed the moduleV2/moduleV3
// branches, since neither field could ever be set anymore) - this phase
// removed the fields themselves.
void RenderFeatureCompositor::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&)
{
    std::vector<Entry> combinedList;
    combinedList.reserve(m_postComposite.size() + m_preUi.size());
    combinedList.insert(combinedList.end(), m_postComposite.begin(), m_postComposite.end());
    combinedList.insert(combinedList.end(), m_preUi.begin(), m_preUi.end());

    // A feature that is effectively disabled (host override, or the toggle
    // registry's own flag for a dual-citizen name like "AtmosphereComposite")
    // is skipped entirely from this frame's compositing chain - the entry
    // itself is never removed from m_postComposite/m_preUi (DebugSnapshot()/
    // GET /render_graph keeps reporting it, disabled).
    combinedList.erase(std::remove_if(combinedList.begin(), combinedList.end(),
        [this](const Entry& entry) {
            return !IsEffectivelyEnabled(entry.descriptor.name, entry.enabledOverride);
        }), combinedList.end());
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

    // Seed the chain: without this, `currentInput` and the LAST entry's own
    // `outputTarget` would be the SAME handle in the SAME dispatch whenever
    // the combined list has exactly one entry - a real GPU hazard (a compute
    // pass reading and writing the exact same storage image in one
    // dispatch). The seed dispatch is always a plain Replace copy.
    m_device = m_renderer.GetVulkanContextInfo().device;

    const char* seedName = m_namePool.SeedName(viewName);
    BlendStageState& seedState = EnsureBlendStageState(seedName, extent);
    // No depth sub-resource - this chain's targets are color-only.
    const rg::TextureHandle seedHandle = frame.builder.ImportTexture(
        seedName, seedState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED, seedState.texture->Sampler());
    DispatchBlend(frame.builder, resolved->target, resolved->sampler, resolved->target, resolved->sampler,
        seedHandle, seedState, m_namePool.SeedCopyPassName(viewName), extent, RenderFeatureBlendMode::Replace,
        kPostProcessingPassTag.bit);

    rg::TextureHandle currentInput = seedHandle;
    VkSampler currentInputSampler = seedState.texture->Sampler();

    for (std::size_t i = 0; i < combinedList.size(); ++i) {
        const Entry& entry = combinedList[i];
        const std::string featureName = entry.descriptor.name; // still used for FindEntryByName()/logs.
        // editor-core-separation-23 campaign, PHASE2
        // (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.4) - a
        // Project Assembly's own render-feature GPU state is NEVER keyed by
        // its human-typed descriptor.name (PHASE0_MASTER_STRATEGY.md's
        // Locked Decision #4) - it uses a bounded, slot-derived key instead.
        const std::string gpuStateKey = (entry.projectCallback)
            ? ("ProjectFeatureSlot" + std::to_string(entry.projectFeatureSlot))
            : featureName;
        const bool isLast = (i + 1 == combinedList.size());

        const char* privateName = m_namePool.PrivateName(gpuStateKey, viewName);
        PrivateTargetState& privateState = EnsurePrivateTargetState(privateName, extent);
        // No depth sub-resource - this chain's targets are color-only.
        const rg::TextureHandle privateTarget = frame.builder.ImportTexture(
            privateName, privateState.texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED, privateState.texture->Sampler());

        if (entry.projectCallback) {
            const std::optional<RenderPassViewData> viewDataOpt = m_core.FindRenderPassViewData(frame.currentView);
            const RenderPassViewData* viewData = viewDataOpt.has_value() ? &(*viewDataOpt) : nullptr;

            // The color half is already a live, resolved handle/sampler pair
            // (this chain's own accumulated "screen so far") - only the depth
            // half (the current view's own depth buffer) needs resolving.
            ScenePassReadHandles handles;
            handles.colorHandle = currentInput;
            handles.colorSampler = currentInputSampler;
            if (viewData != nullptr) {
                const rg::RenderGraphBuilder::ImportedTextureSamplers viewResolved =
                    frame.builder.ResolveImportedTextureSamplers(viewData->colorTarget);
                handles.depthHandle = viewData->colorTarget;
                handles.depthImageView = viewResolved.depthImageView;
                handles.depthSampler = viewResolved.depthSampler;
            }

            const RenderFeatureCameraData cameraData = ResolveRenderFeatureCameraData(frame.currentView, viewData);

            const std::size_t before = frame.builder.DeclaredPassCount();
            entry.projectCallback(
                frame.builder, frame.blackboard, frame.currentView, privateTarget, extent, handles, cameraData);
            const std::size_t after = frame.builder.DeclaredPassCount();

            if (handles.depthSampler != VK_NULL_HANDLE
                && FindMissingDeclaredDepthReadForResolve(frame.builder, before, after, handles.depthHandle)) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    "Render feature '" + featureName + "' declared at least one pass but never declared a "
                    "ReadTexture(..., isDepthResource=true) usage against the depth handle it was given - this is "
                    "an undeclared, unbarriered GPU read if its own execute lambda samples it anyway. See "
                    "docs/conventions/project-assembly-system.md's PostComposite/PreUI subsection.");
                assert(false
                    && "A render feature declared a pass without declaring a matching depth ReadTexture() - see "
                       "the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\").");
            }

            // Every pass a PostComposite/PreUI feature declares must carry
            // RenderPassEvent::AfterEverything - nothing else catches a
            // mistagged-but-still-compiles-and-runs pass here.
            if (const std::vector<std::size_t> mistagged =
                    FindPassesNotTaggedAfterEverything(frame.builder, before, after);
                !mistagged.empty()) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    "Render feature '" + featureName + "' declared " + std::to_string(mistagged.size())
                    + " pass(es) not tagged RenderPassEvent::AfterEverything - every PostComposite/PreUI pass "
                    "must use this exact tag. See docs/conventions/project-assembly-system.md's PostComposite/"
                    "PreUI subsection.");
                assert(false
                    && "A render feature declared a pass with a RenderPassEvent other than AfterEverything - see "
                       "the GTE_LOG_WARNING immediately above (category \"RenderFeatureCompositor\").");
            }
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
            // No depth sub-resource - this chain's targets are color-only.
            outputTarget = frame.builder.ImportTexture(
                accumName, outputState->texture->Target(), VK_IMAGE_LAYOUT_UNDEFINED, outputState->texture->Sampler());
        }

        DispatchBlend(frame.builder, currentInput, currentInputSampler, privateTarget,
            privateState.texture->Sampler(), outputTarget, *outputState, m_namePool.BlendPassName(gpuStateKey, viewName),
            extent, entry.descriptor.blendMode,
            entry.projectCallback ? kProjectFeatureGroupTag.bit : kPostProcessingPassTag.bit);

        currentInput = outputTarget;
        currentInputSampler = isLast ? VK_NULL_HANDLE : outputState->texture->Sampler();
    }

    frame.finalTextureOutputs.push_back(resolved->target);
}

} // namespace gte
