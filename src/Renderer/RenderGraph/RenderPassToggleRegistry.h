#pragma once

// Tracks whether one built-in render pass, identified by its own debug
// name, is currently enabled. Main-thread-only, no mutex - touched only
// from RenderPipeline::DeclareOnePhase(), the Render Graph panel, and
// RenderGraphControlCommandBridge's pump, all on the main thread.

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte::rg {

// One known pass's toggle state - returned by ListAll(), and reported
// 1:1 by GET /render_graph/passes.
struct RenderPassToggleState {
    std::string name;
    bool enabled = true;
    bool everDeclaredThisSession = false; // True once this pass has actually run at least once this session.
};

class RenderPassToggleRegistry {
public:
    RenderPassToggleRegistry() = default;

    // Called once per declared pass, every frame. Auto-registers an
    // unseen name as enabled=true. Returns the current enabled state.
    bool NoteDeclaredAndCheckEnabled(const std::string& name);

    // Called by Core::SetBuiltInRenderPassEnabled(). Upserts `name` if
    // unseen. Returns false, unapplied, if `name` is deny-listed.
    bool SetEnabled(const std::string& name, bool enabled);

    // Current enabled state - true (safe default) if `name` is unknown.
    bool IsEnabled(const std::string& name) const;

    // Plain existence check - NEVER creates an entry, unlike SetEnabled()/
    // NoteDeclaredAndCheckEnabled(). Use this before mirroring a write
    // from an unrelated namespace into SetEnabled() - see
    // RenderFeatureCompositor::SetFeatureEnabled().
    bool HasEntry(const std::string& name) const noexcept
    {
        return m_entries.find(name) != m_entries.end();
    }

    // Every known entry, sorted by name - deterministic UI/JSON order.
    std::vector<RenderPassToggleState> ListAll() const;

    // Names that can never be disabled: "Present" (final swapchain
    // blit) and "ClearViewTarget" (the view's one guaranteed clear).
    static bool IsDenyListed(const std::string& name) noexcept;

    // Max simultaneous entries with everDeclaredThisSession == false. Bounds
    // damage from typo'd/garbage HTTP input; a real pass is never evicted -
    // NoteDeclaredAndCheckEnabled() promotes it out of ghost tracking the
    // moment it actually runs once.
    static constexpr std::size_t kMaxGhostEntries = 256;

private:
    std::unordered_map<std::string, RenderPassToggleState> m_entries;
    std::deque<std::string> m_ghostInsertionOrder; // FIFO, ghost entries only.
};

} // namespace gte::rg
