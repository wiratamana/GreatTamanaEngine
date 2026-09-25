#include "ImGuiIdConflictGuard.h"

#include "../Core/Logging.h"

#include <imgui.h>

#include <string>

namespace gte {

ImGuiIdConflictGuard& ImGuiIdConflictGuard::Instance() noexcept
{
    static ImGuiIdConflictGuard s_instance;
    return s_instance;
}

void ImGuiIdConflictGuard::BeginFrame()
{
    m_frameTracker.Reset();
    // m_ongoingConflicts is deliberately NOT cleared here - see this
    // class's own header comment on CheckCurrentIdScope() for why it must
    // persist across frames (that persistence is what makes "log once per
    // NEW conflict" possible at all).
}

void ImGuiIdConflictGuard::CheckCurrentIdScope(const char* debugContext, const char* debugKey)
{
    const std::uint32_t id = static_cast<std::uint32_t>(ImGui::GetID(""));
    const bool conflictThisFrame = m_frameTracker.RegisterAndCheckConflict(id);

    if (!conflictThisFrame) {
        // Not conflicting THIS frame - if it used to be, it just cleared;
        // erasing it means a FUTURE recurrence is treated as fresh again
        // (re-logged), which is the correct, honest behavior - a resolved
        // and later-reintroduced bug deserves a new alert, not silence
        // forever because it once fired.
        m_ongoingConflicts.erase(id);
        return;
    }

    if (m_ongoingConflicts.insert(id).second) {
        // .second == true means this id was NOT already in the set - i.e.
        // this is a genuinely NEW conflict incident, not a continuation of
        // one already logged last frame. Log exactly once.
        const std::string context = (debugContext != nullptr) ? debugContext : "?";
        const std::string key = (debugKey != nullptr) ? debugKey : "";
        GTE_LOG_ERROR("ImGuiIdConflict",
            "Duplicate ImGui widget ID detected within a single frame - context='" + context + "', key='" + key
                + "'. Two different ScopedUniqueId scopes resolved to the exact same underlying ImGui ID this "
                  "frame. This means the calling loop reused the same index for two different rows, or two "
                  "independent call sites collided - check the index the offending loop passed to ScopedUniqueId. "
                  "(This is caught proactively, before Dear ImGui's own built-in visual red-highlight would ever "
                  "fire for the same collision.)");
    }
}

} // namespace gte
