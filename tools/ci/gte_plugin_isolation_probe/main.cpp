// tools/ci/gte_plugin_isolation_probe/main.cpp
//
// editor-core-separation-3 campaign, PHASE5
// (PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md) - Milestone 3: proves,
// mechanically (not just by code-reading), that "always all-in" (source
// design doc Section 0/7) still keeps a genuinely Player-shaped process
// editor-clean. This probe links gte_core.a ALONE (no gte_editor, no ImGui,
// no SDL - confirmed via this probe's own CMakeLists.txt using
// GTE_CORE_STANDALONE_PROBE_ONLY=ON, mirroring
// tools/ci/gte_core_player_link_probe's own precedent), uses gte::PluginHost
// directly (a plain gte_core-owned class with zero GPU/window dependency of
// its own - PluginHost::LoadPlugins() only ever does LoadLibraryW()/
// GetProcAddress()/a byte-for-byte fingerprint compare, never touches
// Vulkan/SDL), and queries ONLY IRenderFeatureModule_v1 on every loaded
// module.
//
// THIS FILE INTENTIONALLY, PERMANENTLY NEVER MENTIONS IEditorPanelModule_v1
// ANYWHERE IN ITS OWN SOURCE - that absence of a call IS the isolation proof
// itself: an editor-tier plugin (demo_editor_panel.dll) sitting in plugins/
// is loaded here (its fingerprint/export handshake still succeeds -
// PluginHost does not distinguish "runtime" from "editor" plugins at LOAD
// time, only at QUERY time, per PluginHost.h's own class comment), but
// nothing in this whole file ever asks it for anything - it sits there
// fully inert, never crashing, never producing any visible effect. There is
// nothing to assert at runtime about a call that never happens; the proof
// is structural and checkable by any human reader of this small main.cpp.
//
// Reuses gte::PluginHost directly rather than re-implementing
// LoadLibraryW()/fingerprint-checking a second time -
// tools/ci/gte_plugin_abi_handshake_probe/main.cpp (PHASE2) already proves
// the raw ABI handshake in complete isolation from gte_core; THIS probe's
// own job is proving the real, production PluginHost class (and, through
// it, IRenderFeatureModule_v1's capability-query mechanism) behaves
// identically once linked into a genuinely Player-shaped host.

#include "../../../src/Core/Plugins/PluginHost.h"
#include "../../../plugins/gte_plugin_abi/IRenderFeatureModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

// This probe's own known build-tree layout (mirrors
// tools/ci/gte_plugin_abi_handshake_probe/main.cpp's own
// ResolveDemoHelloWorldDllPath() precedent, PHASE2): resolved via
// GetModuleFileNameW - THIS EXE'S OWN DIRECTORY - rather than
// std::filesystem::current_path(), since a process's current working
// directory at execution time is whatever its CALLER happened to set,
// never guaranteed to be this exe's own directory. This is a deliberate,
// real correction to PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md's own
// literal Step 3.1 sketch (which used std::filesystem::current_path()) -
// see this phase's own PHASE5_COMPLETION_REPORT.md for the full reasoning.
//
// Both this .exe and the four demo plugin .dll's are built by the SAME
// nested, GTE_CORE_STANDALONE_PROBE_ONLY=ON inner configure - this .exe
// lands directly in that inner build's own CMAKE_BINARY_DIR (no
// RUNTIME_OUTPUT_DIRECTORY override, mirroring GreatTamanaEditor.exe's own
// plain add_executable() precedent), and every demo plugin .dll is copied
// into "<inner-build-dir>/plugins/" (GTE_PLUGIN_RUNTIME_OUTPUT_DIR's own
// GTE_CORE_STANDALONE_PROBE_ONLY=ON branch, root CMakeLists.txt) - i.e.
// directly next to this .exe's own directory, under a "plugins" subfolder.
std::filesystem::path ResolvePluginsDirectory()
{
    wchar_t exePathBuffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    const std::filesystem::path exePath(std::wstring(exePathBuffer, length));
    return exePath.parent_path() / "plugins";
}

} // namespace

int main()
{
    gte::PluginHost host;
    host.LoadPlugins(ResolvePluginsDirectory());

    // Explicitly rule out a false-positive pass caused by a folder-path
    // mistake (this phase's own Step 4 point 3 requires this exact check,
    // not merely "> 0") - all FOUR demo plugins (demo_hello_world,
    // demo_render_feature, demo_render_feature_second, demo_editor_panel)
    // must genuinely load (editor-core-separation-4 campaign, PHASE5 -
    // PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md -
    // added demo_render_feature_second, was 3 plugins, now 4).
    if (host.LoadedModuleCount() != 4) {
        std::fprintf(stderr, "FAIL: expected exactly 4 demo plugins to load, loaded %zu.\n", host.LoadedModuleCount());
        return 1;
    }

    int renderFeatureCount = 0;
    for (gte::IPluginModule* module : host.AllLoadedModules()) {
        gte::GtePluginModuleInfo info;
        module->GetModuleInfo(info);
        const bool hasRenderFeature = module->QueryCapability(gte::kIRenderFeatureModule_v1_Name) != nullptr;
        std::printf("Loaded '%s' - IRenderFeatureModule_v1: %s\n", info.name, hasRenderFeature ? "yes" : "no");
        if (hasRenderFeature) {
            ++renderFeatureCount;
        }
    }

    if (renderFeatureCount != 2) {
        std::fprintf(stderr, "FAIL: expected exactly 2 plugins implementing IRenderFeatureModule_v1, found %d.\n", renderFeatureCount);
        return 1;
    }

    std::printf("PASS: %zu plugin(s) loaded, exactly 2 implement IRenderFeatureModule_v1, "
                "and this file never once asked any plugin for IEditorPanelModule_v1.\n",
        host.LoadedModuleCount());
    return 0;
}
