#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

class IEditorPanelModule_v1;

// Replaces EditorPanelCatalog.h's fixed kKnownEditorPanelNames[] array
// (editor-core-separation-3 campaign, PHASE4) - source design doc Section
// 6: "DockLayout/EditorHost hold a registry populated at startup by (a) the
// engine's own small set of always-present, built-in panels registering
// themselves the exact same way a plugin would, plus (b) whatever
// IEditorPanelModule_v1 capabilities every loaded plugin .dll exposes."
//
// A process-wide singleton (Meyers-singleton, mirrors LoggerLogSink::
// Instance()'s own established precedent in this codebase) - deliberately
// NOT a Core-owned member, since NetworkRoutes.h/.cpp (gte_core-tier, but
// with NO reference to a live Core instance at its own call sites - see
// NetworkServer's own existing bridge-pointer-based design) needs to query
// it too, exactly like EditorPanelCatalog.h's own free functions did
// before this phase.
class EditorPanelRegistry {
public:
    static EditorPanelRegistry& Instance();

    // Called exactly once per built-in panel, at process startup, BEFORE
    // any plugin is ever loaded (see EditorHost.cpp's own new
    // registration call, PHASE4 Step 3.6) - mirrors
    // kKnownEditorPanelNames[]'s own former fixed content exactly, so
    // every existing built-in panel's own name is registered identically
    // to before this phase.
    void RegisterBuiltinPanelName(const std::string& name);

    // Called once per loaded plugin exposing IEditorPanelModule_v1,
    // immediately after Core::LoadPlugins() returns (gte_editor's own new
    // post-load step, PHASE4 Step 3.6) - `module` is a non-owning pointer
    // into PluginHost's own registry, valid until EITHER process exit OR an
    // explicit UnregisterPluginPanel(name) call for that same name, whichever
    // comes first (editor-core-separation-13 campaign, BIG-STEP 2, PHASE2 -
    // previously this comment said PluginHost never unloads a plugin before
    // process exit; that is no longer the whole story once
    // UnregisterPluginPanel() below exists and is actually called by
    // ProjectAssemblyRegistrationLedger::UnregisterEverythingFor() and
    // ProjectAssemblyHost::UnloadProjectAssembly(), both this same
    // campaign's PHASE3/PHASE4).
    void RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module);

    // editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 2, Hazard 2 fix) - removes a previously-registered plugin panel
    // by name, if present, from BOTH m_pluginPanels (the vector holding the
    // dangerous raw IEditorPanelModule_v1* - unsafe to leave dangling past a
    // FreeLibrary()) AND m_allNames (the plain-string list IsKnownName()'s own
    // collision guard reads - leaving a stale entry there would permanently
    // block this exact same name from EVER being registered again, which is
    // precisely what every reload after the first needs to do; confirmed a
    // real, guaranteed regression, not a hypothetical one - see this campaign's
    // PHASE2 strategy file for the full reasoning). MUST be called for every
    // panel name a Project Assembly's own GTE_RegisterProject call registered,
    // BEFORE that Project Assembly's .dll is FreeLibrary()'d - see
    // ProjectAssemblyRegistrationLedger (src/Core/Plugins/
    // ProjectAssemblyRegistrationLedger.h, this same campaign's PHASE3) for the
    // mechanism that guarantees this automatically. A silent no-op if `name`
    // was never registered, or was already removed.
    //
    // NOTE this INTENTIONALLY DEVIATES from this feature's own external design
    // doc (HOTRELOAD_BIGSTEP_02_TEARDOWN_SAFETY_AND_REGISTRATION_LEDGER_2026-09-28.txt,
    // Step 2), which left "does this also clean up m_allNames" as an open
    // question deferred to a later phase - that deferral was incorrect (see
    // this campaign's PHASE2 strategy file); this method removes from BOTH
    // vectors, unconditionally.
    void UnregisterPluginPanel(const std::string& name);

    bool IsKnownName(const std::string& name) const noexcept;

    // Every registered name, built-in AND plugin, in registration order -
    // replaces kKnownEditorPanelNames[]'s own former iteration role for
    // DockLayout.cpp's "has every panel been docked" check and
    // NetworkRoutes.cpp's GET /list_tabs.
    const std::vector<std::string>& AllNames() const noexcept { return m_allNames; }

    // Just the plugin-registered subset, in registration order - what
    // DockLayout.cpp's generic bottom-dock loop and ImGuiEditorLayer.cpp's
    // generic BuildUI() loop both iterate. Empty whenever no loaded Project
    // Assembly `_Editor.dll` implements this capability (the ABI-versioned
    // `plugins/gte_plugin_abi` system that used to be this registry's OTHER
    // populating source was fully removed by the `better-render-pass-2`
    // campaign) - a fully safe, empty-by-default state.
    struct PluginPanelEntry {
        std::string name;
        IEditorPanelModule_v1* module = nullptr;
    };
    const std::vector<PluginPanelEntry>& PluginPanels() const noexcept { return m_pluginPanels; }

private:
    EditorPanelRegistry() = default;

    std::vector<std::string> m_allNames;
    std::vector<PluginPanelEntry> m_pluginPanels;
};

} // namespace gte
