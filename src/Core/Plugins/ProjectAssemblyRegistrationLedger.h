// src/Core/Plugins/ProjectAssemblyRegistrationLedger.h
//
// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2), PHASE3. Records every render-pass debugName / editor-panel
// name / ECS component typeName a SPECIFIC Project Assembly's own
// GTE_RegisterProject call registered, with ZERO change required to that
// Project Assembly's own authored code - Core::RegisterProjectRenderPassProvider()/
// EditorPanelRegistry::RegisterPluginPanel()/ComponentTypeRegistry::
// RegisterDescriptor() each call this class's own RecordX() immediately
// after doing their own real work, unconditionally, every time they are
// called by ANYTHING (including the engine's own built-in startup
// registrations) - RecordX() itself is a safe, silent no-op whenever no
// BeginRecordingFor() bracket is currently active, which is true for every
// one of the engine's own built-in registrations (they never run inside
// such a bracket).
//
// Locking: every public method takes THIS CLASS'S OWN internal m_mutex -
// deliberately NOT gte::GetHotReloadEngineStateMutex() (src/Core/Plugins/
// HotReloadEngineStateMutex.h) - m_mutex only protects this class's own
// m_activeProjectStack/m_entries data. A caller that also needs to guard
// the WIDER engine state (ComponentTypeRegistry/EditorPanelRegistry/Core's
// own render pipeline) around a RecordX()/UnregisterEverythingFor()/
// PeekEntry() call is responsible for taking GetHotReloadEngineStateMutex()
// itself, exactly like EditorHotReloadDebugCapability::GetLedgerEntry()
// already does around its own PeekEntry() call - this class itself must
// NEVER also lock GetHotReloadEngineStateMutex() internally, since that
// non-recursive mutex is already held by that exact caller at the point it
// calls in, which would deadlock immediately.
#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace gte {

class Core;

class ProjectAssemblyRegistrationLedger {
public:
    static ProjectAssemblyRegistrationLedger& Instance();

    // Called by ProjectAssemblyHost::TryLoadOneAssembly() immediately BEFORE
    // invoking a specific project's GTE_RegisterProject export (both the
    // initial startup load AND, from a future BIG-STEP 3 campaign onward,
    // every reload) - every RecordRenderPass()/RecordPanel()/
    // RecordComponentType() call, from ANY thread, until EndRecording() is
    // called, is attributed to `projectName`. Nests safely via a plain
    // stack push/pop: EndRecording() always pairs with the MOST RECENT
    // BeginRecordingFor() - relevant because a _Game.dll's own
    // GTE_RegisterProject and its sibling _Editor.dll's own
    // GTE_RegisterProject are two SEPARATE calls for the SAME projectName,
    // both accumulating into the SAME ledger entry.
    void BeginRecordingFor(const std::string& projectName);
    void EndRecording();

    // Called by Core::RegisterProjectRenderPassProvider()/
    // EditorPanelRegistry::RegisterPluginPanel()/ComponentTypeRegistry::
    // RegisterDescriptor() themselves, immediately after each does its own
    // real work - ALWAYS, unconditionally, from every call site, including
    // the engine's own built-in startup registrations. A safe, silent no-op
    // whenever no BeginRecordingFor() bracket is currently active (the
    // engine's own built-in registrations never run inside one).
    void RecordRenderPass(const std::string& debugName);
    // editor-core-separation-23 campaign, PHASE4
    // (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - called by
    // Core::RegisterProjectRenderFeature() on ITS OWN success path only
    // (unlike RecordRenderPass(), which Core::RegisterProjectRenderPassProvider()
    // calls unconditionally, since that underlying call can never itself
    // fail) - mirrors RecordRenderPass()'s own body shape exactly.
    void RecordRenderFeature(const std::string& debugName);
    // better-render-pass-5 effort, BLOCK 3, PHASE4 - mirrors
    // RecordRenderFeature()'s own body shape exactly. Called ONLY from
    // Core::AddPreOpaquePass()'s own success path (that call can
    // genuinely fail - duplicate name), never unconditionally.
    void RecordPreOpaqueFeature(const std::string& debugName);
    // Called ONLY from Core::AddPostOpaquePass()'s own success path - that
    // call can genuinely fail (duplicate name), so no over-eager/spurious
    // name ever lands in the ledger.
    void RecordPostOpaqueFeature(const std::string& debugName);
    // Same contract, for Core::AddPostTransparentPass().
    void RecordPostTransparentFeature(const std::string& debugName);
    void RecordPanel(const std::string& panelName);
    void RecordComponentType(const std::string& typeName);

    struct Entry {
        std::vector<std::string> renderPassNames;
        // editor-core-separation-23 campaign, PHASE4 - a Project Assembly's
        // own on-screen render feature name(s) (Core::RegisterProjectRenderFeature(),
        // editor-core-separation-23 campaign). Field ORDER here is not itself
        // semantically load-bearing - only UnregisterEverythingFor()'s own
        // real teardown LOOP order (renderFeatureNames before renderPassNames)
        // is.
        std::vector<std::string> renderFeatureNames;
        std::vector<std::string> postOpaqueFeatureNames;
        std::vector<std::string> postTransparentFeatureNames;
        // better-render-pass-5 effort, BLOCK 3, PHASE4 - a Project
        // Assembly's own PreOpaque feature name(s)
        // (Core::AddPreOpaquePass(), better-render-pass-5 effort). Torn
        // down in the SAME teardown pass as renderFeatureNames,
        // immediately before it (see UnregisterEverythingFor()'s own
        // ordering comment, .cpp) - a PreOpaque feature is itself a pure
        // producer with no dependency, in THIS teardown call, on any
        // other Project-Assembly-registered category also being torn
        // down here.
        std::vector<std::string> preOpaqueFeatureNames;
        std::vector<std::string> panelNames;
        std::vector<std::string> componentTypeNames;
    };

    // The teardown step (Hazards 1/2 fix, BIG-STEP 0 Section 2) - walks
    // `projectName`'s own recorded names, in REVERSE registration order,
    // calling core.GetRenderPipelineForProjectAssemblies().Unregister() (see
    // 3.3 below for the exact accessor this needs from Core),
    // EditorPanelRegistry::Instance().UnregisterPluginPanel(), and
    // ComponentTypeRegistry::Instance().UnregisterDescriptor() for each, then
    // erases `projectName`'s own ledger entry entirely (a fresh
    // BeginRecordingFor(projectName) on the next load/reload starts from a
    // genuinely empty slate). Idempotent - calling this for a projectName
    // with no ledger entry (never loaded, or already torn down) is a safe,
    // GTE_LOG_INFO-logged no-op, never a warning or crash - this is an
    // expected, normal path a future BIG-STEP 3 campaign's own retry/failure
    // handling relies on.
    void UnregisterEverythingFor(const std::string& projectName, Core& core);

    // Read-only peek at projectName's current ledger entry, for
    // EditorHotReloadDebugCapability::GetLedgerEntry() (GET
    // /project_assembly/debug/ledger). Returns a VALUE COPY of an empty
    // Entry (never a reference/pointer) for a projectName with no current
    // entry - a debug HTTP caller never needs to special-case "never
    // loaded" versus "loaded, but registered nothing"; both look identical
    // (all three vectors empty). Never mutates anything.
    Entry PeekEntry(const std::string& projectName) const;

private:
    ProjectAssemblyRegistrationLedger() = default;

    mutable std::mutex m_mutex;
    std::vector<std::string> m_activeProjectStack; // supports nested Begin/End (Game then Editor).
    std::vector<std::pair<std::string, Entry>> m_entries; // one per known projectName, insertion order.

    Entry& GetOrCreateEntryLocked(const std::string& projectName); // caller already holds m_mutex.
};

} // namespace gte
