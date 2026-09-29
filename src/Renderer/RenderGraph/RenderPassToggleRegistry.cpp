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
        // editor-core-separation-21 campaign, PHASE1 - TEMPORARY diagnostic
        // instrumentation (category "RenderPassHonestyDiag", see
        // task_manager/editor-core-separation-21/PHASE1_LIVE_REPRODUCTION_AND_ROOT_CAUSE_DIAGNOSIS.md
        // Step 3.3) - restricted to the two toggle names this phase is
        // actively diagnosing, to avoid drowning the 2000-entry ring buffer
        // in noise from every other pass's own per-frame declare call.
        if (name == "AtmosphereAerialPerspectiveCompositePass" || name == "AtmosphereComposite") {
            GTE_LOG_DEBUG("RenderPassHonestyDiag",
                "NoteDeclaredAndCheckEnabled(\"" + name + "\") FIRST-SEEN this session -> enabled="
                    + std::string(it->second.enabled ? "true" : "false"));
        }
        return it->second.enabled;
    }
    it->second.everDeclaredThisSession = true;
    if (name == "AtmosphereAerialPerspectiveCompositePass" || name == "AtmosphereComposite") {
        GTE_LOG_DEBUG("RenderPassHonestyDiag",
            "NoteDeclaredAndCheckEnabled(\"" + name + "\") -> enabled=" + std::string(it->second.enabled ? "true" : "false"));
    }
    return it->second.enabled;
}

bool RenderPassToggleRegistry::SetEnabled(const std::string& name, bool enabled)
{
    if (IsDenyListed(name)) {
        return false;
    }
    // editor-core-separation-21 campaign, PHASE1 - TEMPORARY diagnostic
    // instrumentation, same "RenderPassHonestyDiag" category/scoping as
    // NoteDeclaredAndCheckEnabled() above - logs the mutation itself,
    // BEFORE it is applied, so its own before/after state is captured.
    const bool isDiagnosedName = (name == "AtmosphereAerialPerspectiveCompositePass" || name == "AtmosphereComposite");
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        if (isDiagnosedName) {
            GTE_LOG_DEBUG("RenderPassHonestyDiag",
                "SetEnabled(\"" + name + "\", " + std::string(enabled ? "true" : "false")
                    + ") - entry did not exist yet, creating with everDeclaredThisSession=false");
        }
        RenderPassToggleState state;
        state.name = name;
        state.enabled = enabled;
        state.everDeclaredThisSession = false;
        m_entries.emplace(name, std::move(state));
        return true;
    }
    if (isDiagnosedName) {
        GTE_LOG_DEBUG("RenderPassHonestyDiag",
            "SetEnabled(\"" + name + "\", " + std::string(enabled ? "true" : "false") + ") - before: enabled="
                + std::string(it->second.enabled ? "true" : "false")
                + ", everDeclaredThisSession=" + std::string(it->second.everDeclaredThisSession ? "true" : "false"));
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
    // editor-core-separation-20 campaign, PHASE1 - "ClearViewTarget" added:
    // the ONE guaranteed clear of the Game/Scene View target every frame -
    // disabling it would defeat the entire fix this pass exists for (see
    // task_manager/editor-core-separation-20/PHASE0_MASTER_STRATEGY.md's
    // Root Cause #1) with no in-process recovery, exactly like "Present".
    return name == "Present" || name == "ClearViewTarget";
}

} // namespace gte::rg
