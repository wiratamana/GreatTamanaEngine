// src/Core/Plugins/ProjectAssemblyRegistrationLedger.cpp
//
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3. See ProjectAssemblyRegistrationLedger.h for the full
// design rationale. #includes ComponentTypeRegistry.h/EditorPanelRegistry.h/
// Core.h ONLY here, in the .cpp - keeps the header free of these, matching
// this codebase's own "only the .cpp needs the complete type" convention
// (e.g. EditorHotReloadDebugCapability.h's own precedent).
#include "ProjectAssemblyRegistrationLedger.h"

#include "../Core.h"
#include "../EditorPanelRegistry.h"
#include "../Logging.h"
#include "../../ECS/Reflection/ComponentTypeRegistry.h"

#include <algorithm>
#include <cassert>

namespace gte {

ProjectAssemblyRegistrationLedger& ProjectAssemblyRegistrationLedger::Instance()
{
    static ProjectAssemblyRegistrationLedger instance;
    return instance;
}

ProjectAssemblyRegistrationLedger::Entry& ProjectAssemblyRegistrationLedger::GetOrCreateEntryLocked(const std::string& projectName)
{
    for (auto& pair : m_entries) {
        if (pair.first == projectName) {
            return pair.second;
        }
    }
    m_entries.emplace_back(projectName, Entry{});
    return m_entries.back().second;
}

void ProjectAssemblyRegistrationLedger::BeginRecordingFor(const std::string& projectName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeProjectStack.push_back(projectName);
}

void ProjectAssemblyRegistrationLedger::EndRecording()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    assert(!m_activeProjectStack.empty() && "ProjectAssemblyRegistrationLedger::EndRecording() called with no matching BeginRecordingFor() - always a programmer error");
    if (m_activeProjectStack.empty()) {
        return; // Release-build safety net - never crash, even though this should never happen.
    }
    m_activeProjectStack.pop_back();
}

void ProjectAssemblyRegistrationLedger::RecordRenderPass(const std::string& debugName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeProjectStack.empty()) {
        return; // Safe no-op - no active BeginRecordingFor() bracket (e.g. an engine built-in registration).
    }
    GetOrCreateEntryLocked(m_activeProjectStack.back()).renderPassNames.push_back(debugName);
}

// editor-core-separation-23 campaign, PHASE4
// (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - mirrors RecordRenderPass()'s
// own body shape exactly. Called ONLY from Core::RegisterProjectRenderFeature()'s
// own success path (that call can genuinely fail - duplicate name/unwired
// stage/slot exhaustion - unlike RegisterProjectRenderPassProvider(), which
// never fails), so no over-eager/spurious name ever lands in the ledger.
void ProjectAssemblyRegistrationLedger::RecordRenderFeature(const std::string& debugName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeProjectStack.empty()) {
        return; // Safe no-op - no active BeginRecordingFor() bracket (e.g. an engine built-in registration).
    }
    GetOrCreateEntryLocked(m_activeProjectStack.back()).renderFeatureNames.push_back(debugName);
}

void ProjectAssemblyRegistrationLedger::RecordPanel(const std::string& panelName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeProjectStack.empty()) {
        return; // Safe no-op - no active BeginRecordingFor() bracket.
    }
    GetOrCreateEntryLocked(m_activeProjectStack.back()).panelNames.push_back(panelName);
}

void ProjectAssemblyRegistrationLedger::RecordComponentType(const std::string& typeName)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_activeProjectStack.empty()) {
        return; // Safe no-op - no active BeginRecordingFor() bracket.
    }
    GetOrCreateEntryLocked(m_activeProjectStack.back()).componentTypeNames.push_back(typeName);
}

void ProjectAssemblyRegistrationLedger::UnregisterEverythingFor(const std::string& projectName, Core& core)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [&projectName](const std::pair<std::string, Entry>& pair) { return pair.first == projectName; });
    if (it == m_entries.end()) {
        GTE_LOG_INFO("ProjectAssemblyRegistrationLedger",
            "UnregisterEverythingFor('" + projectName + "') - no ledger entry found (never loaded, or already torn "
            "down) - safe no-op.");
        return;
    }

    Entry& entry = it->second;

    // Reverse registration order for each category - the mirror image of
    // how construction typically unwinds (last-registered, first-torn-down).
    for (auto nameIt = entry.componentTypeNames.rbegin(); nameIt != entry.componentTypeNames.rend(); ++nameIt) {
        // editor-core-separation-15 campaign (Project Assembly Hot Reload
        // plan, BIG-STEP 4), PHASE5 - destroy this custom component type's
        // own ComponentStorage<T> pool BEFORE removing its descriptor and
        // BEFORE the caller's own subsequent FreeLibrary() - see
        // Registry::ResetStoragePool<T>()'s own doc comment (ECS/Registry.h)
        // for the full "why" (a confirmed, live, gdb-diagnosed crash this
        // closes).
        if (const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find(*nameIt);
            descriptor != nullptr && descriptor->destroyPool) {
            descriptor->destroyPool(core.GetGame().GetRegistry());
        }
        ComponentTypeRegistry::Instance().UnregisterDescriptor(*nameIt);
    }
    for (auto nameIt = entry.panelNames.rbegin(); nameIt != entry.panelNames.rend(); ++nameIt) {
        EditorPanelRegistry::Instance().UnregisterPluginPanel(*nameIt);
    }
    // editor-core-separation-23 campaign, PHASE4
    // (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - render FEATURES are
    // torn down BEFORE render-pass PROVIDERS: a render feature's own
    // callback may reference a project's own offscreen render-pass output
    // via the blackboard, so the CONSUMER (the render feature) must be
    // torn down before the PRODUCER it may depend on (the render-pass
    // provider) - the same cross-category dependency direction already
    // established by componentTypeNames -> panelNames above. This also
    // releases each torn-down feature's own claimed GPU-state slot
    // (RenderFeatureCompositor::UnregisterProjectFeature()'s own existing
    // contract, PHASE2) back to the free list as part of the SAME call -
    // no separate slot bookkeeping needed here.
    for (auto nameIt = entry.renderFeatureNames.rbegin(); nameIt != entry.renderFeatureNames.rend(); ++nameIt) {
        core.UnregisterProjectRenderFeature(nameIt->c_str());
    }
    for (auto nameIt = entry.renderPassNames.rbegin(); nameIt != entry.renderPassNames.rend(); ++nameIt) {
        core.UnregisterProjectRenderPassProvider(nameIt->c_str());
    }

    GTE_LOG_INFO("ProjectAssemblyRegistrationLedger",
        "UnregisterEverythingFor('" + projectName + "') - unregistered " +
        std::to_string(entry.componentTypeNames.size()) + " component type(s), " +
        std::to_string(entry.panelNames.size()) + " panel(s), " +
        std::to_string(entry.renderFeatureNames.size()) + " render feature(s), " +
        std::to_string(entry.renderPassNames.size()) + " render pass(es).");

    m_entries.erase(it);
}

ProjectAssemblyRegistrationLedger::Entry ProjectAssemblyRegistrationLedger::PeekEntry(const std::string& projectName) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& pair : m_entries) {
        if (pair.first == projectName) {
            return pair.second;
        }
    }
    return Entry{};
}

} // namespace gte
