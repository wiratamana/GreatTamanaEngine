#pragma once

#include "ImGuiIdConflictTracker.h"

#include <cstdint>
#include <unordered_set>

// Forward-declare rather than #include <imgui.h> here - keeps this header
// cheap for any future file that only needs to call CheckCurrentIdScope()
// via ImGuiUniqueId.h (PHASE2), which is the only production call site.
// ImGuiIdConflictGuard.cpp includes <imgui.h> for real.

namespace gte {

// Editor-owned, process-global, main-thread-only singleton - mirrors
// LoggerLogSink::Instance()'s exact shape (src/Editor/Logger.h) for the
// same reason: a call site can be anywhere under src/Editor/, with no
// single natural owner to thread a reference through, and (unlike Logger
// itself) this is legitimately main-thread-only, since Dear ImGui itself
// is main-thread-only in this engine.
//
// Wraps ImGuiIdConflictTracker (the pure, Tier-1-tested detector) with two
// real-world concerns the pure tracker deliberately knows nothing about:
// (1) resolving a REAL Dear ImGui ID via ImGui::GetID(), and (2) deciding
// WHEN to actually log a conflict - see CheckCurrentIdScope()'s own comment
// for the "log once per NEW conflict, not once per frame" rule (Locked
// Design Decision #2, PHASE0_MASTER_STRATEGY.md). Deliberately untested by
// any automated test (Tier 2 - needs a live ImGui context to call
// ImGui::GetID() at all, and a live installed Logger sink to observe its
// own logging side effect) - exactly the same "pure logic tested, ImGui/
// Vulkan-owning wrapper not" split AGENTS.md already documents for
// GpuMemoryTracker vs every Vulkan-owning class.
class ImGuiIdConflictGuard {
public:
    static ImGuiIdConflictGuard& Instance() noexcept;

    // Call exactly once per real UI frame, AFTER ImGui::NewFrame() (see
    // ImGuiEditorLayer::NewFrame()). Resets the per-frame tracker; does
    // NOT reset which conflicts are considered "ongoing" (see
    // CheckCurrentIdScope() - that is what makes "log once per new
    // conflict" work across frames).
    void BeginFrame();

    // Called by ScopedUniqueId's constructor (ImGuiUniqueId.h, PHASE2),
    // exactly once per ID scope entered, immediately AFTER the relevant
    // ImGui::PushID() call(s) for that scope. Resolves the CURRENT ImGui ID
    // stack position via ImGui::GetID("") and checks it against this
    // frame's tracker.
    //
    // `debugContext` is a short, human-readable call site description
    // (e.g. "RenderGraphPanel::BuildPassRow") - always a string literal at
    // the call site, never freed/dangling. `debugKey` is the semantic
    // string this scope's uniqueness argument was ALSO built from (e.g. a
    // pass name) - used ONLY for the log message text if a conflict is
    // found, NEVER relied upon for uniqueness/hashing itself (may be
    // nullptr or empty).
    //
    // Logs via GTE_LOG_ERROR("ImGuiIdConflict", ...) - but only the FIRST
    // time a given real ImGui ID starts conflicting; if the exact same
    // conflict is still happening next frame too, it stays silent (no
    // spam) until the conflict actually clears for at least one frame and
    // then reappears (a genuinely NEW incident, worth a fresh log line) -
    // Locked Design Decision #2.
    void CheckCurrentIdScope(const char* debugContext, const char* debugKey);

private:
    ImGuiIdConflictGuard() = default;

    ImGuiIdConflictTracker m_frameTracker;                // reset every BeginFrame()
    std::unordered_set<std::uint32_t> m_ongoingConflicts; // persists ACROSS frames
};

} // namespace gte
