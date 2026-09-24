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
    // into PluginHost's own registry, valid for the engine's entire
    // remaining lifetime (PluginHost never unloads a plugin before process
    // exit - Milestone 4/hot-reload is explicitly out of scope, see
    // PHASE0_MASTER_STRATEGY.md's Non-Goals).
    void RegisterPluginPanel(const std::string& name, IEditorPanelModule_v1* module);

    bool IsKnownName(const std::string& name) const noexcept;

    // Every registered name, built-in AND plugin, in registration order -
    // replaces kKnownEditorPanelNames[]'s own former iteration role for
    // DockLayout.cpp's "has every panel been docked" check and
    // NetworkRoutes.cpp's GET /list_tabs.
    const std::vector<std::string>& AllNames() const noexcept { return m_allNames; }

    // Just the plugin-registered subset, in registration order - what
    // DockLayout.cpp's new generic bottom-dock loop and
    // ImGuiEditorLayer.cpp's new generic BuildUI() loop both iterate (PHASE4
    // Step 3.5/3.7). Empty whenever GTE_ENABLE_PLUGINS is OFF or no
    // loaded plugin implements this capability - a fully safe, empty-by-
    // default state.
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
