// src/Core/Plugins/ProjectAssemblyRegistrationLedger.h
// Records the render passes, windows and component types that each project assembly registers, so they can be
// unregistered on unload. Recording is a no-op outside a BeginRecordingFor() bracket.
// Locking: uses its own m_mutex only. Callers that also need GetHotReloadEngineStateMutex() must take it themselves;
// this class must never lock it (the caller already holds it, so it would deadlock).
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

    // Called by each registry after it registers something. No-op when no BeginRecordingFor() bracket is active.
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

    // Unregisters everything `projectName` recorded, in reverse order, then erases its entry.
    // Safe no-op (logged) when there is no entry.
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
