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

// Sorts an on-screen list (PostComposite or PreUI) by descriptor priority.
template <class E>
void SortOnScreenEntries(std::vector<E>& entries, const char* stageName)
{
    SortByPriorityWarnOnTies(
        entries, stageName, [](const E& e) { return e.descriptor.priority; },
        [](const E& e) { return std::string_view(e.descriptor.name); });
}

} // namespace

RenderFeatureCompositor::Impl::Impl(Core& coreRef)
    : core(coreRef)
{
    for (int i = 0; i < kMaxConcurrentProjectRenderFeatures; ++i) {
        freeProjectFeatureSlots.push_back(i);
    }
}

RenderFeatureCompositor::Entry* RenderFeatureCompositor::Impl::FindEntryByName(const std::string& name)
{
    std::vector<Entry>* list = EntryListOf(name);
    if (list == nullptr) {
        return nullptr;
    }
    const auto it = std::find_if(
        list->begin(), list->end(), [&name](const Entry& e) { return name == e.descriptor.name; });
    return &*it;
}

std::vector<RenderFeatureCompositor::Entry>* RenderFeatureCompositor::Impl::EntryListOf(const std::string& name)
{
    const auto holds = [&name](const std::vector<Entry>& list) {
        return std::any_of(list.begin(), list.end(), [&name](const Entry& e) { return name == e.descriptor.name; });
    };
    if (holds(postComposite)) {
        return &postComposite;
    }
    return holds(preUi) ? &preUi : nullptr;
}

const char* RenderFeatureCompositor::Impl::StageOfName(const std::string& name)
{
    if (const std::vector<Entry>* list = EntryListOf(name)) {
        return list == &postComposite ? "PostComposite" : "PreUI";
    }
    const char* found = nullptr;
    ForEachStage([&](auto& list, const char* stageName) {
        if (found == nullptr && list.Find(name) != nullptr) {
            found = stageName;
        }
    });
    return found;
}

bool RenderFeatureCompositor::Impl::IsEffectivelyEnabled(const std::string& name, bool enabledOverride) const
{
    if (!enabledOverride) {
        return false;
    }
    const rg::RenderPassToggleRegistry& toggles = core.GetRenderPassToggleRegistryMutable();
    return !toggles.HasEntry(name) || toggles.IsEnabled(name);
}

void RenderFeatureCompositor::Impl::AssertCalledFromMainThread() const
{
    assert(std::this_thread::get_id() == mainThreadId
        && "Feature registration is main-thread-only (like ContributeRenderGraphPasses()); this call came from "
           "another thread.");
}

bool RenderFeatureCompositor::Impl::SetFeatureEnabled(const std::string& name, bool enabled)
{
    bool found = false;
    if (Entry* entry = FindEntryByName(name)) {
        entry->enabledOverride = enabled;
        found = true;
    } else {
        ForEachStage([&](auto& list, const char*) { found = found || list.SetEnabled(name, enabled); });
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
        const bool isPostComposite = EntryListOf(name) == &postComposite;
        SortOnScreenEntries(isPostComposite ? postComposite : preUi, isPostComposite ? "PostComposite" : "PreUI");
        return true;
    }

    bool found = false;
    ForEachStage(
        [&](auto& list, const char* stageName) { found = found || list.SetPriority(name, priority, stageName); });
    return found;
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

    if (const char* existingStage = StageOfName(descriptor.name)) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string("RegisterFeature('") + descriptor.name + "') refused - a " + existingStage
            + " feature with this name is already registered (feature names share one global namespace across "
              "every stage).");
        return false;
    }

    if (descriptor.stage == RenderFeatureStage::PreOpaque || descriptor.stage == RenderFeatureStage::PostOpaque
        || descriptor.stage == RenderFeatureStage::PostTransparent) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            std::string(descriptor.name) + " declared a RenderFeatureStage that is not wired for "
            "RegisterFeature() - this feature will not run any frame.");
        return false;
    }

    const int claimedSlot = freeProjectFeatureSlots.back();
    freeProjectFeatureSlots.pop_back();

    Entry entry;
    entry.projectCallback = std::move(callback);
    entry.descriptor = descriptor;
    entry.projectFeatureSlot = claimedSlot;
    entry.owner = owner;

    const bool isPreUi = (entry.descriptor.stage == RenderFeatureStage::PreUI);
    std::vector<Entry>& targetStage = isPreUi ? preUi : postComposite;
    targetStage.push_back(std::move(entry));
    SortOnScreenEntries(targetStage, isPreUi ? "PreUI" : "PostComposite");

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

    const std::string nameStr = name;
    Entry* entry = FindEntryByName(nameStr);
    if (entry == nullptr) {
        return false;
    }

    if (!entry->projectCallback) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "UnregisterProjectFeature('" + nameStr + "') refused - this entry is not a Project Assembly render "
            "feature.");
        return false;
    }

    freeProjectFeatureSlots.push_back(entry->projectFeatureSlot);

    std::vector<Entry>& list = *EntryListOf(nameStr);
    list.erase(std::remove_if(list.begin(), list.end(),
                   [&nameStr](const Entry& e) { return nameStr == e.descriptor.name; }),
        list.end());

    GTE_LOG_INFO("RenderFeatureCompositor",
        "UnregisterProjectFeature('" + nameStr + "') succeeded - released GPU-state slot back to the free list.");
    return true;
}

namespace {

// Shared body of the stage registrations; `ImplT` is the compositor's private Impl.
template <class ImplT, class List, class Callback>
bool AddToStage(ImplT& impl, List& list, const char* stageName, const std::string& name, std::int32_t priority,
    Callback&& callback, RenderFeatureOwner owner)
{
    if (const char* existingStage = impl.StageOfName(name)) {
        GTE_LOG_WARNING("RenderFeatureCompositor",
            "RegisterStageFeature('" + name + "') refused - a " + existingStage + " feature with this name is "
            "already registered (feature names share one global namespace across every stage).");
        return false;
    }
    list.Add(name, priority, std::forward<Callback>(callback), owner, stageName);
    GTE_LOG_INFO("RenderFeatureCompositor", "RegisterStageFeature('" + name + "') succeeded in stage " + stageName + ".");
    return true;
}

void LogStageCallbackMismatch(const std::string& name, const char* callbackKind)
{
    GTE_LOG_WARNING("RenderFeatureCompositor",
        "RegisterStageFeature('" + name + "') refused - the stage does not take a " + callbackKind + " callback.");
    assert(false && "RegisterStageFeature() stage and callback type do not match.");
}

} // namespace

bool RenderFeatureCompositor::Impl::RegisterStageFeature(CompositorStage stage, const std::string& name,
    std::int32_t priority, ProjectPreOpaqueCallback callback, RenderFeatureOwner owner)
{
    AssertCalledFromMainThread();
    if (stage != CompositorStage::PreOpaque) {
        LogStageCallbackMismatch(name, "pre-opaque");
        return false;
    }
    return AddToStage(*this, preOpaque, "PreOpaque", name, priority, std::move(callback), owner);
}

bool RenderFeatureCompositor::Impl::RegisterStageFeature(CompositorStage stage, const std::string& name,
    std::int32_t priority, ProjectScenePassCallback callback, RenderFeatureOwner owner)
{
    AssertCalledFromMainThread();
    switch (stage) {
    case CompositorStage::PostOpaque:
        return AddToStage(*this, postOpaque, "PostOpaque", name, priority, std::move(callback), owner);
    case CompositorStage::PostTransparent:
        return AddToStage(*this, postTransparent, "PostTransparent", name, priority, std::move(callback), owner);
    case CompositorStage::PreOpaque:
        break;
    }
    LogStageCallbackMismatch(name, "scene-pass");
    return false;
}

bool RenderFeatureCompositor::Impl::UnregisterStageFeature(CompositorStage stage, const char* name)
{
    AssertCalledFromMainThread();

    if (name == nullptr) {
        return false;
    }

    const std::string nameStr = name;
    return VisitStage(stage, [&nameStr](auto& list, const char* stageName) {
        if (!list.Remove(nameStr)) {
            GTE_LOG_WARNING("RenderFeatureCompositor",
                "UnregisterStageFeature('" + nameStr + "') refused - no such " + stageName
                + " feature is registered.");
            return false;
        }
        GTE_LOG_INFO("RenderFeatureCompositor", "UnregisterStageFeature('" + nameStr + "') succeeded in stage "
            + stageName + ".");
        return true;
    });
}

void RenderFeatureCompositor::Impl::OnPluginsLoaded(const std::vector<IPluginModule*>& /*modules*/)
{
}

std::vector<RenderFeatureDebugEntry> RenderFeatureCompositor::Impl::DebugSnapshot() const
{
    std::vector<RenderFeatureDebugEntry> snapshot;
    snapshot.reserve(postComposite.size() + preUi.size() + preOpaque.entries.size() + postOpaque.entries.size()
        + postTransparent.entries.size());

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

    const auto isEnabled = [this](const std::string& name, bool enabledOverride) {
        return IsEffectivelyEnabled(name, enabledOverride);
    };
    ForEachStage([&](const auto& list, const char* stageName) { list.AppendSnapshot(snapshot, stageName, isEnabled); });

    return snapshot;
}

} // namespace gte
