#include "RenderPassToggleRegistry.h"

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
    it->second.everDeclaredThisSession = true;
    return it->second.enabled;
}

bool RenderPassToggleRegistry::SetEnabled(const std::string& name, bool enabled)
{
    if (IsDenyListed(name)) {
        return false;
    }
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        RenderPassToggleState state;
        state.name = name;
        state.enabled = enabled;
        state.everDeclaredThisSession = false;
        m_entries.emplace(name, std::move(state));
        return true;
    }
    it->second.enabled = enabled;
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
    return name == "Present";
}

} // namespace gte::rg
