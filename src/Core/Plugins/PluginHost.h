#pragma once

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gte {

// gte_core's own Plugin Host (source design doc, Section 1/4.1) - scans a
// folder for *.dll, LoadLibraryW()s each, checks the fingerprint, and holds
// the resulting registry. Mechanical and CAPABILITY-AGNOSTIC: this class has
// ZERO built-in knowledge of IRenderFeatureModule_v1/IEditorPanelModule_v1 or
// any other capability - every actual feature interaction happens through
// QueryCapability() results, requested by capability-consuming code
// elsewhere (Core::RegisterOffscreenRenderPipelineProviders() - PHASE3;
// gte_editor's DockLayout - PHASE4), never by this class itself.
//
// Exactly ONE instance exists per process (PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #8) - owned by Core, populated once via LoadPlugins()
// (Locked Design Decision #9), never re-scanned per frame.
class PluginHost {
public:
    PluginHost() = default;
    ~PluginHost();

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    // Enumerates every *.dll directly inside `pluginsDirectory` (no
    // recursion into subfolders), attempts to load each per the source
    // design doc's Section 4.1 procedure. Safe to call with a
    // non-existent directory (logs one line, loads nothing, does not
    // throw) - a Player-shaped host with no plugins/ folder at all must
    // never crash. Safe to call more than once (idempotent per call -
    // each call scans fresh and APPENDS to the existing registry; calling
    // it twice with the same folder will load every .dll twice as two
    // separate module instances - callers must only ever call this once
    // per process, exactly as PHASE0's Locked Design Decision #9 requires;
    // this method itself does not defend against a caller violating that,
    // matching PHASE0's own "load once, at host-construction time" rule
    // being an explicitly documented CALLER discipline, not a class
    // invariant this class enforces itself).
    void LoadPlugins(const std::filesystem::path& pluginsDirectory);

    // Every module that successfully passed the fingerprint check and
    // returned a non-null IPluginModule* from GTE_CreatePluginModule().
    const std::vector<IPluginModule*>& AllLoadedModules() const noexcept { return m_modules; }

    std::size_t LoadedModuleCount() const noexcept { return m_modules.size(); }

private:
    struct LoadedPlugin {
        void* moduleHandle = nullptr; // HMODULE, stored as void* so this header never needs <windows.h>
        IPluginModule* module = nullptr;
        // The plugin's own exported GTE_DestroyPluginModule, resolved once
        // at load time, called exactly once, from ~PluginHost(), in
        // REVERSE load order (last-loaded, first-destroyed) - mirrors
        // ordinary RAII/stack-unwind destruction order convention.
        void (*destroyFn)(IPluginModule*) = nullptr;
    };

    // Real implementation lives entirely in PluginHost.cpp - keeps
    // <windows.h> and every plugins/gte_plugin_abi/PluginExports.h
    // function-pointer typedef out of this header.
    void TryLoadOnePlugin(const std::filesystem::path& dllPath);

    // PHASE1 (editor-core-separation-4 campaign) - logs ONE loud
    // GTE_LOG_WARNING, at most once per PluginHost instance, the first time
    // LoadPlugins() runs, if-and-only-if this build's own fingerprint has
    // sharedRuntimeLinkage == 0 - see GtePluginAbiFingerprint.h's own
    // corrected doc comment on that field for the full reasoning. A no-op
    // (correctly silent) on a build that genuinely achieved shared CRT
    // linkage.
    void LogSharedCrtRiskWarningOnce();

    std::vector<LoadedPlugin> m_loadedPlugins;
    std::vector<IPluginModule*> m_modules; // parallel, public-facing view - m_loadedPlugins[i].module == m_modules[i]
    bool m_sharedCrtRiskWarningLogged = false;
};

} // namespace gte
