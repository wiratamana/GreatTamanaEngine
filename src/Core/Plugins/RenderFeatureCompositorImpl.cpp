#include "RenderFeatureCompositorImpl.h"

#include "../Core.h"
#include "../Logging.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

namespace gte {

namespace {

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

RenderFeatureCompositor::Impl::Impl(Core& coreRef)
    : core(coreRef)
{
    for (int i = 0; i < kMaxConcurrentProjectRenderFeatures; ++i) {
        freeProjectFeatureSlots.push_back(i);
    }
}

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

RenderFeatureCompositor::Entry* RenderFeatureCompositor::Impl::FindEntryByName(const std::string& name)
{
    for (Entry& entry : postComposite) {
        if (name == entry.descriptor.name) {
            return &entry;
        }
    }
    for (Entry& entry : preUi) {
        if (name == entry.descriptor.name) {
            return &entry;
        }
    }
    return nullptr;
}

bool RenderFeatureCompositor::Impl::IsEffectivelyEnabled(const std::string& name, bool enabledOverride) const
{
    if (!enabledOverride) {
        return false;
    }
    const rg::RenderPassToggleRegistry& toggles = core.GetRenderPassToggleRegistryMutable();
    return !toggles.HasEntry(name) || toggles.IsEnabled(name);
}

RenderFeatureCompositor::PreOpaqueEntry* RenderFeatureCompositor::Impl::FindPreOpaqueEntryByName(
    const std::string& name)
{
    for (PreOpaqueEntry& entry : preOpaque) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

RenderFeatureCompositor::PostOpaqueEntry* RenderFeatureCompositor::Impl::FindPostOpaqueEntryByName(
    const std::string& name)
{
    for (PostOpaqueEntry& entry : postOpaque) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

RenderFeatureCompositor::PostTransparentEntry* RenderFeatureCompositor::Impl::FindPostTransparentEntryByName(
    const std::string& name)
{
    for (PostTransparentEntry& entry : postTransparent) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}

void RenderFeatureCompositor::Impl::AssertCalledFromMainThread() const
{
    assert(std::this_thread::get_id() == mainThreadId
        && "RenderFeatureCompositor::RegisterProjectFeature()/UnregisterProjectFeature() are documented "
           "main-thread-only contracts (exactly like ContributeRenderGraphPasses() itself) - this call came from a "
           "different thread.");
}

bool RenderFeatureCompositor::Impl::SetFeatureEnabled(const std::string& name, bool enabled)
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

    rg::RenderPassToggleRegistry& toggles = core.GetRenderPassToggleRegistryMutable();
    if (toggles.HasEntry(name)) {
        toggles.SetEnabled(name, enabled);
    }
    return true;
}

bool RenderFeatureCompositor::Impl::SetFeaturePriority(const std::string& name, std::int32_t priority)
{
    if (Entry* entry = FindEntryByName(name)) {
        entry->descriptor.priority = priority;

        const bool isPostComposite =
            std::find_if(postComposite.begin(), postComposite.end(),
                [&name](const Entry& e) { return name == e.descriptor.name; })
            != postComposite.end();
        if (isPostComposite) {
            SortAndDetectCollisionsInStage(postComposite, "PostComposite");
        } else {
            SortAndDetectCollisionsInStage(preUi, "PreUI");
        }
        return true;
    }

    if (PreOpaqueEntry* preOpaqueEntry = FindPreOpaqueEntryByName(name)) {
        preOpaqueEntry->priority = priority;
        SortAndDetectCollisionsInPreOpaqueList(preOpaque);
        return true;
    }

    if (PostOpaqueEntry* postOpaqueEntry = FindPostOpaqueEntryByName(name)) {
        postOpaqueEntry->priority = priority;
        SortAndDetectCollisionsInPostOpaqueList(postOpaque);
        return true;
    }
    if (PostTransparentEntry* postTransparentEntry = FindPostTransparentEntryByName(name)) {
        postTransparentEntry->priority = priority;
        SortAndDetectCollisionsInPostTransparentList(postTransparent);
        return true;
    }

    return false;
}

bool RenderFeatureCompositor::Impl::RegisterFeature(
    const GtePluginRenderFeatureDescriptor& descriptor, ProjectRenderFeatureCallback callback, RenderFeatureOwner owner)
{
    AssertCalledFromMainThread();

    if (freeProjectFeatureSlots.empty()) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterFeature('") + descriptor.name + "') refused - every one of the "
            + std::to_string(kMaxConcurrentProjectRenderFeatures)
            + " kMaxConcurrentProjectRenderFeatures slots is currently claimed by another still-registered "
              "render feature.");
        return false;
    }

    if (FindEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterFeature('") + descriptor.name
            + "') refused - a render feature with this name is already registered.");
        return false;
    }

    if (FindPreOpaqueEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterFeature('") + descriptor.name
            + "') refused - a PreOpaque feature with this name is already registered (feature names share one "
            "global namespace across every stage).");
        return false;
    }

    if (FindPostOpaqueEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterFeature('") + descriptor.name
            + "') refused - a PostOpaque feature with this name is already registered (feature names share one "
            "global namespace across every stage).");
        return false;
    }
    if (FindPostTransparentEntryByName(descriptor.name) != nullptr) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterFeature('") + descriptor.name
            + "') refused - a PostTransparent feature with this name is already registered (feature names share "
            "one global namespace across every stage).");
        return false;
    }

    const int claimedSlot = freeProjectFeatureSlots.back();
    freeProjectFeatureSlots.pop_back();

    Entry entry;
    entry.projectCallback = std::move(callback);
    entry.descriptor = descriptor;
    entry.projectFeatureSlot = claimedSlot;
    entry.owner = owner;

    if (entry.descriptor.stage == RenderFeatureStage::PreOpaque
        || entry.descriptor.stage == RenderFeatureStage::PostOpaque
        || entry.descriptor.stage == RenderFeatureStage::PostTransparent) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string(entry.descriptor.name) + " declared a RenderFeatureStage that is not wired for "
            "RegisterFeature() - this feature will not run any frame.");
        freeProjectFeatureSlots.push_back(claimedSlot);
        return false;
    }

    const bool isPreUi = (entry.descriptor.stage == RenderFeatureStage::PreUI);
    std::vector<Entry>& targetStage = isPreUi ? preUi : postComposite;
    targetStage.push_back(std::move(entry));
    SortAndDetectCollisionsInStage(targetStage, isPreUi ? "PreUI" : "PostComposite");

    GTE_LOG_INFO("RenderFeatureCompositor",
        std::string("RegisterFeature('") + descriptor.name + "') succeeded - claimed GPU-state slot "
        + std::to_string(claimedSlot) + ".");
    return true;
}

bool RenderFeatureCompositor::Impl::UnregisterProjectFeature(const char* name)
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
            "Assembly render feature.");
        return false;
    }

    freeProjectFeatureSlots.push_back(entry->projectFeatureSlot);

    const std::string nameStr = name;
    const bool isPostComposite =
        std::find_if(postComposite.begin(), postComposite.end(),
            [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; })
        != postComposite.end();
    if (isPostComposite) {
        postComposite.erase(std::remove_if(postComposite.begin(), postComposite.end(),
                                 [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; }),
            postComposite.end());
    } else {
        preUi.erase(std::remove_if(preUi.begin(), preUi.end(),
                        [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; }),
            preUi.end());
    }

    GTE_LOG_INFO("RenderFeatureCompositor",
        std::string("UnregisterProjectFeature('") + name + "') succeeded - released GPU-state slot back to the "
        "free list.");
    return true;
}

bool RenderFeatureCompositor::Impl::RegisterPreOpaqueFeatureInternal(
    const std::string& name, std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner)
{
    AssertCalledFromMainThread();

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
    entry.owner = owner;
    preOpaque.push_back(std::move(entry));
    SortAndDetectCollisionsInPreOpaqueList(preOpaque);

    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterPreOpaqueFeature('" + name + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::Impl::UnregisterPreOpaqueFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    const std::size_t sizeBefore = preOpaque.size();
    preOpaque.erase(
        std::remove_if(preOpaque.begin(), preOpaque.end(), [&nameStr](const PreOpaqueEntry& e) { return nameStr == e.name; }),
        preOpaque.end());

    if (preOpaque.size() == sizeBefore) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterPreOpaqueFeature('" + nameStr + "') refused - no such PreOpaque feature is registered.");
        return false;
    }

    GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterPreOpaqueFeature('" + nameStr + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::Impl::RegisterPostOpaqueFeatureInternal(
    const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner)
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
    entry.owner = owner;
    postOpaque.push_back(std::move(entry));
    SortAndDetectCollisionsInPostOpaqueList(postOpaque);

    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterPostOpaqueFeature('" + name + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::Impl::UnregisterPostOpaqueFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    const std::size_t sizeBefore = postOpaque.size();
    postOpaque.erase(std::remove_if(postOpaque.begin(), postOpaque.end(),
                          [&nameStr](const PostOpaqueEntry& e) { return nameStr == e.name; }),
        postOpaque.end());

    if (postOpaque.size() == sizeBefore) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterPostOpaqueFeature('" + nameStr + "') refused - no such PostOpaque feature is registered.");
        return false;
    }

    GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterPostOpaqueFeature('" + nameStr + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::Impl::RegisterPostTransparentFeatureInternal(
    const std::string& name, std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner)
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
    entry.owner = owner;
    postTransparent.push_back(std::move(entry));
    SortAndDetectCollisionsInPostTransparentList(postTransparent);

    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterPostTransparentFeature('" + name + "') succeeded.");
    return true;
}

bool RenderFeatureCompositor::Impl::UnregisterPostTransparentFeature(const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    const std::size_t sizeBefore = postTransparent.size();
    postTransparent.erase(std::remove_if(postTransparent.begin(), postTransparent.end(),
                               [&nameStr](const PostTransparentEntry& e) { return nameStr == e.name; }),
        postTransparent.end());

    if (postTransparent.size() == sizeBefore) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterPostTransparentFeature('" + nameStr
            + "') refused - no such PostTransparent feature is registered.");
        return false;
    }

    GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterPostTransparentFeature('" + nameStr + "') succeeded.");
    return true;
}

void RenderFeatureCompositor::Impl::OnPluginsLoaded(const std::vector<IPluginModule*>& /*modules*/)
{
}

std::vector<RenderFeatureDebugEntry> RenderFeatureCompositor::Impl::DebugSnapshot() const
{
    std::vector<RenderFeatureDebugEntry> snapshot;
    snapshot.reserve(postComposite.size() + preUi.size() + preOpaque.size() + postOpaque.size()
        + postTransparent.size());

    auto appendStage = [&snapshot, this](const std::vector<Entry>& entries) {
        for (const Entry& entry : entries) {
            RenderFeatureDebugEntry debugEntry;
            debugEntry.name = entry.descriptor.name;
            debugEntry.stage = ToString(entry.descriptor.stage);
            debugEntry.priority = entry.descriptor.priority;
            debugEntry.blendMode = ToString(entry.descriptor.blendMode);
            debugEntry.enabled = IsEffectivelyEnabled(entry.descriptor.name, entry.enabledOverride);
            debugEntry.isProjectFeature = (entry.owner == RenderFeatureOwner::Project);
            snapshot.push_back(std::move(debugEntry));
        }
    };

    appendStage(postComposite);
    appendStage(preUi);

    for (const PreOpaqueEntry& entry : preOpaque) {
        RenderFeatureDebugEntry debugEntry;
        debugEntry.name = entry.name;
        debugEntry.stage = "PreOpaque";
        debugEntry.priority = entry.priority;
        debugEntry.blendMode = "None";
        debugEntry.enabled = IsEffectivelyEnabled(entry.name, entry.enabledOverride);
        debugEntry.isProjectFeature = (entry.owner == RenderFeatureOwner::Project);
        snapshot.push_back(std::move(debugEntry));
    }

    for (const PostOpaqueEntry& entry : postOpaque) {
        RenderFeatureDebugEntry debugEntry;
        debugEntry.name = entry.name;
        debugEntry.stage = "PostOpaque";
        debugEntry.priority = entry.priority;
        debugEntry.blendMode = "None";
        debugEntry.enabled = IsEffectivelyEnabled(entry.name, entry.enabledOverride);
        debugEntry.isProjectFeature = (entry.owner == RenderFeatureOwner::Project);
        snapshot.push_back(std::move(debugEntry));
    }
    for (const PostTransparentEntry& entry : postTransparent) {
        RenderFeatureDebugEntry debugEntry;
        debugEntry.name = entry.name;
        debugEntry.stage = "PostTransparent";
        debugEntry.priority = entry.priority;
        debugEntry.blendMode = "None";
        debugEntry.enabled = IsEffectivelyEnabled(entry.name, entry.enabledOverride);
        debugEntry.isProjectFeature = (entry.owner == RenderFeatureOwner::Project);
        snapshot.push_back(std::move(debugEntry));
    }

    return snapshot;
}


} // namespace gte
