#pragma once

// Enabled state and owning feature of each declared render pass, keyed by pass name.
// Main-thread only, no mutex. Lookups take string_view and never allocate.

#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gte::rg {

// One known pass's toggle state - returned by ListAll(), and reported 1:1
// by GET /render_graph/passes.
struct RenderPassToggleState {
    std::string name;
    bool enabled = true;
    bool everDeclaredThisSession = false; // True once this pass has actually run at least once this session.
    // Sticky: set on first sight, never overwritten after. "ENGINE_UNOWNED"
    // only for a pass declared with no active RenderFeatureScope at all -
    // that should never happen in a real session.
    std::string owningFeatureName;
};

class RenderPassToggleRegistry {
public:
    RenderPassToggleRegistry() = default;

    // Called only by RenderGraphBuilder::AddRenderPass()/AddBlitPass().
    // Auto-registers an unseen name as enabled with the given owner. No-op for an empty name.
    void NoteDeclaredWithOwner(std::string_view name, std::string_view owningFeatureName);

    // Upserts `name` if unseen. Returns false, unapplied, if `name` is deny-listed.
    bool SetEnabled(const std::string& name, bool enabled);

    // Current enabled state - true (safe default) if `name` is unknown.
    bool IsEnabled(std::string_view name) const;

    // Plain existence check; never creates an entry.
    bool HasEntry(std::string_view name) const noexcept { return m_entries.find(name) != m_entries.end(); }

    // Every known entry, sorted by name - deterministic UI/JSON order.
    std::vector<RenderPassToggleState> ListAll() const;

    // Names that can never be disabled: "Present" and "ClearViewTarget".
    static bool IsDenyListed(const std::string& name) noexcept;

    // Max simultaneous never-declared (ghost) entries; bounds damage from typo'd HTTP input.
    static constexpr std::size_t kMaxGhostEntries = 256;

private:
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
    };

    std::unordered_map<std::string, RenderPassToggleState, NameHash, std::equal_to<>> m_entries;
    std::deque<std::string> m_ghostInsertionOrder; // FIFO, ghost entries only.
};

} // namespace gte::rg
