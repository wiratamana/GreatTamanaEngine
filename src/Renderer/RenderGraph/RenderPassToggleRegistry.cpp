#include "RenderPassToggleRegistry.h"

#include "../../Core/Logging.h"

#include <algorithm>

namespace gte::rg {

void RenderPassToggleRegistry::NoteDeclaredWithOwner(std::string_view name, std::string_view owningFeatureName)
{
    if (name.empty()) {
        return;
    }
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        RenderPassToggleState state;
        state.name = std::string(name);
        state.enabled = true;
        state.everDeclaredThisSession = true;
        state.owningFeatureName = std::string(owningFeatureName);
        std::string key = state.name;
        m_entries.emplace(std::move(key), std::move(state));
        return;
    }
    if (!it->second.everDeclaredThisSession) {
        // A ghost proved itself real: stop tracking it so it can never be evicted.
        std::erase(m_ghostInsertionOrder, it->first);
    }
    it->second.everDeclaredThisSession = true;
    if (it->second.owningFeatureName.empty()) {
        it->second.owningFeatureName = std::string(owningFeatureName); // First sight wins.
    }
}

bool RenderPassToggleRegistry::SetEnabled(const std::string& name, bool enabled)
{
    if (IsDenyListed(name)) {
        return false;
    }
    auto it = m_entries.find(name);
    if (it != m_entries.end()) {
        it->second.enabled = enabled;
        return true;
    }

    // Unknown name: a pre-disable of a pass that has not run yet, or a typo. Accept it, but cap the damage.
    if (m_ghostInsertionOrder.size() >= kMaxGhostEntries) {
        const std::string oldest = m_ghostInsertionOrder.front();
        m_ghostInsertionOrder.pop_front();
        m_entries.erase(oldest);
        GTE_LOG_WARNING("RenderPassToggleRegistry",
            "Evicted ghost pass toggle '" + oldest + "' - kMaxGhostEntries ("
            + std::to_string(kMaxGhostEntries) + ") reached.");
    }

    RenderPassToggleState state;
    state.name = name;
    state.enabled = enabled;
    state.everDeclaredThisSession = false;
    m_entries.emplace(name, std::move(state));
    m_ghostInsertionOrder.push_back(name);

    GTE_LOG_WARNING("RenderPassToggleRegistry",
        "SetEnabled('" + name + "') created a never-declared-this-session entry - confirm this is a real pass "
        "name, not a typo.");
    return true;
}

bool RenderPassToggleRegistry::IsEnabled(std::string_view name) const
{
    const auto it = m_entries.find(name);
    return it == m_entries.end() ? true : it->second.enabled;
}

std::vector<RenderPassToggleState> RenderPassToggleRegistry::ListAll() const
{
    std::vector<RenderPassToggleState> result;
    result.reserve(m_entries.size());
    for (const auto& [name, state] : m_entries) {
        result.push_back(state);
    }
    std::sort(result.begin(), result.end(),
        [](const RenderPassToggleState& a, const RenderPassToggleState& b) { return a.name < b.name; });
    return result;
}

bool RenderPassToggleRegistry::IsDenyListed(const std::string& name) noexcept
{
    // Disabling either would leave a view with no safe fallback and no in-process recovery.
    return name == "Present" || name == "ClearViewTarget";
}

} // namespace gte::rg
