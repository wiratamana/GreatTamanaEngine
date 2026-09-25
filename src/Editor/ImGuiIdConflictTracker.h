#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace gte {

// Pure, Tier-1-testable id-collision detector - see AGENTS.md's "ImGui
// Widget ID Uniqueness" section and
// task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md for the
// full story (the Render Graph Panel duplicate-checkbox bug this exists to
// permanently prevent a recurrence of). Deliberately has ZERO dependency on
// <imgui.h> or Logger - it operates purely on plain std::uint32_t values the
// caller already resolved. See ImGuiIdConflictGuard (ImGuiIdConflictGuard.h)
// for the real ImGui/Logger-aware wrapper built on top of this class - that
// is the one production call sites actually use; this class exists
// separately, with this narrow a contract, specifically so it can be tested
// with no live ImGui context at all (see
// tests/Editor/ImGuiIdConflictTrackerTests.cpp).
class ImGuiIdConflictTracker {
public:
    // Clears every id recorded since the last Reset() call - call exactly
    // once per real UI frame. Production call site: ImGuiIdConflictGuard::
    // BeginFrame(), itself called from ImGuiEditorLayer::NewFrame().
    void Reset();

    // Records `id` as seen since the last Reset() and returns true if it was
    // ALREADY recorded earlier during this same Reset()..Reset() window
    // (i.e. THIS call is a genuine conflict - two different call sites
    // resolved to the exact same id before the next Reset()). Recording
    // always happens regardless of the return value, so a third or fourth
    // occurrence of the same id keeps correctly being reported as a
    // conflict too, not just the second occurrence.
    bool RegisterAndCheckConflict(std::uint32_t id);

    // Number of distinct ids recorded since the last Reset() - exposed
    // purely for tests/diagnostics; no production call site needs this.
    std::size_t DistinctIdCount() const noexcept;

private:
    std::unordered_set<std::uint32_t> m_seenIds;
};

} // namespace gte
