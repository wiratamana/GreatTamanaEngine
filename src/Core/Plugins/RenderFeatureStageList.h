#pragma once

// Priority-ordered feature list of one scene stage; only RenderFeatureCompositorImpl.h includes it.

#include "RenderFeatureCompositor.h"
#include "RenderFeatureDebugEntry.h"
#include "../Logging.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte {

// Stable sort by priority; equal priorities warn and fall back to a lexical name tie-break.
template <class Entry, class PriorityOf, class NameOf>
void SortByPriorityWarnOnTies(
    std::vector<Entry>& entries, const char* stageName, PriorityOf priorityOf, NameOf nameOf)
{
    std::stable_sort(entries.begin(), entries.end(),
        [&](const Entry& a, const Entry& b) { return priorityOf(a) < priorityOf(b); });

    std::size_t i = 0;
    while (i < entries.size()) {
        std::size_t j = i;
        while (j + 1 < entries.size() && priorityOf(entries[j + 1]) == priorityOf(entries[i])) {
            ++j;
        }
        if (j > i) {
            for (std::size_t k = i; k < j; ++k) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    std::string(nameOf(entries[k])) + " and " + std::string(nameOf(entries[k + 1]))
                    + " both declared priority " + std::to_string(priorityOf(entries[k])) + " in stage " + stageName
                    + " - this is ambiguous; falling back to a stable, lexical name tie-break. "
                    "Assign each feature a distinct priority to remove this warning.");
            }
            std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(i),
                entries.begin() + static_cast<std::ptrdiff_t>(j) + 1,
                [&](const Entry& a, const Entry& b) { return nameOf(a) < nameOf(b); });
        }
        i = j + 1;
    }
}

template <class Callback>
struct StageList {
    using EntryType = StageEntry<Callback>;

    std::vector<EntryType> entries; // Sorted by priority ascending.

    EntryType* Find(const std::string& name)
    {
        const auto it = std::find_if(entries.begin(), entries.end(),
            [&name](const EntryType& e) { return e.name == name; });
        return it != entries.end() ? &*it : nullptr;
    }

    void Add(const std::string& name, std::int32_t priority, Callback callback, RenderFeatureOwner owner,
        const char* stageName)
    {
        EntryType entry;
        entry.name = name;
        entry.priority = priority;
        entry.callback = std::move(callback);
        entry.owner = owner;
        entries.push_back(std::move(entry));
        Sort(stageName);
    }

    bool Remove(const std::string& name)
    {
        const std::size_t sizeBefore = entries.size();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                          [&name](const EntryType& e) { return e.name == name; }),
            entries.end());
        return entries.size() != sizeBefore;
    }

    bool SetEnabled(const std::string& name, bool enabled)
    {
        EntryType* entry = Find(name);
        if (entry == nullptr) {
            return false;
        }
        entry->enabledOverride = enabled;
        return true;
    }

    bool SetPriority(const std::string& name, std::int32_t priority, const char* stageName)
    {
        EntryType* entry = Find(name);
        if (entry == nullptr) {
            return false;
        }
        entry->priority = priority;
        Sort(stageName);
        return true;
    }

    template <class IsEnabled>
    void AppendSnapshot(std::vector<RenderFeatureDebugEntry>& out, const char* stageName, IsEnabled&& isEnabled) const
    {
        for (const EntryType& entry : entries) {
            RenderFeatureDebugEntry debugEntry;
            debugEntry.name = entry.name;
            debugEntry.stage = stageName;
            debugEntry.priority = entry.priority;
            debugEntry.blendMode = "None";
            debugEntry.enabled = isEnabled(entry.name, entry.enabledOverride);
            debugEntry.isProjectFeature = (entry.owner == RenderFeatureOwner::Project);
            out.push_back(std::move(debugEntry));
        }
    }

private:
    void Sort(const char* stageName)
    {
        SortByPriorityWarnOnTies(
            entries, stageName, [](const EntryType& e) { return e.priority; },
            [](const EntryType& e) { return std::string_view(e.name); });
    }
};

} // namespace gte
