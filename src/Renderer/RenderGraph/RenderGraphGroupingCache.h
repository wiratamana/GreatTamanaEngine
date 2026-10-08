#pragma once
#include <string>
#include <unordered_map>

namespace gte::rg {

// Remembers each pass name's last-seen tag-group label across frames, so a
// disabled/not-running pass still groups under its real heading instead of
// falling into "Ungrouped". Pure data, no ImGui dependency.
class RenderGraphGroupingCache {
public:
    void Observe(const std::string& passName, const std::string& groupLabel)
    {
        m_lastKnownGroupLabel[passName] = groupLabel;
    }

    const std::string& Resolve(const std::string& passName) const
    {
        static const std::string kUngrouped = "Ungrouped";
        const auto it = m_lastKnownGroupLabel.find(passName);
        return it != m_lastKnownGroupLabel.end() ? it->second : kUngrouped;
    }

    void ResetForTesting() { m_lastKnownGroupLabel.clear(); }
    std::size_t SizeForTesting() const noexcept { return m_lastKnownGroupLabel.size(); }

private:
    std::unordered_map<std::string, std::string> m_lastKnownGroupLabel;
};

} // namespace gte::rg
