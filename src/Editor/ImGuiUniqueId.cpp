#include "ImGuiUniqueId.h"

#include "ImGuiIdConflictGuard.h"

#include <imgui.h>

namespace gte {

ScopedUniqueId::ScopedUniqueId(int index, const char* debugContext, const char* debugKey)
    : m_pushedDebugKeyScope(debugKey != nullptr && debugKey[0] != '\0')
{
    // The index-based push is the ONLY thing this class relies on for
    // actual uniqueness - a loop iteration index is always distinct across
    // the SAME loop's iterations in the SAME frame, by construction,
    // regardless of what any data string contains.
    ImGui::PushID(index);

    // Purely cosmetic/for-debuggability nested scope - makes the composed
    // ID stack more legible under Dear ImGui's own Item Picker, and gives
    // ImGuiIdConflictGuard a real key string to put in its log message if a
    // conflict is ever still detected. Never relied upon for uniqueness -
    // the index push above already guarantees that on its own.
    if (m_pushedDebugKeyScope) {
        ImGui::PushID(debugKey);
    }

    ImGuiIdConflictGuard::Instance().CheckCurrentIdScope(debugContext, debugKey);
}

ScopedUniqueId::~ScopedUniqueId()
{
    if (m_pushedDebugKeyScope) {
        ImGui::PopID();
    }
    ImGui::PopID();
}

} // namespace gte
