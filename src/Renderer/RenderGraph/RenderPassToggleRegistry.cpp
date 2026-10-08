#include "RenderPassToggleRegistry.h"

#include "../../Core/Logging.h"

#include <algorithm>

namespace gte::rg {

bool RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled(const std::string& name)
{
    if (name.empty()) {
        return true;
    }
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        RenderPassToggleState state;
        state.name = name;
        state.enabled = true;
        state.everDeclaredThisSession = true;
        it = m_entries.emplace(name, std::move(state)).first;
        return it->second.enabled;
    }
    if (!it->second.everDeclaredThisSession) {
        // Promote: this was a ghost (set via SetEnabled before ever
        // running) - it just proved itself real. Drop it from ghost
        // tracking so it can never be evicted.
        std::erase(m_ghostInsertionOrder, name);
    }
    it->second.everDeclaredThisSession = true;
    return it->second.enabled;
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

    // Unknown name: either a legitimate pre-disable of a pass that has not
    // run yet this session, or a typo from an HTTP client. Cannot tell them
    // apart - accept it, but cap the damage.
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

bool RenderPassToggleRegistry::IsEnabled(const std::string& name) const
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
    // editor-core-separation-20 campaign, PHASE1 - "ClearViewTarget" added:
    // the ONE guaranteed clear of the Game/Scene View target every frame -
    // disabling it would defeat the entire fix this pass exists for (see
    // task_manager/editor-core-separation-20/PHASE0_MASTER_STRATEGY.md's
    // Root Cause #1) with no in-process recovery, exactly like "Present".
    return name == "Present" || name == "ClearViewTarget";
}

} // namespace gte::rg
