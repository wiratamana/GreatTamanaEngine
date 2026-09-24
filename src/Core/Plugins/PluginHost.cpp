#include "PluginHost.h"

#include "../Logging.h"

#include "../../../plugins/gte_plugin_abi/PluginExports.h"
#include "FixedBufferReader.h"
// PHASE1_COMPLETION_REPORT.md's own "Minor note" - the generated fingerprint
// header does NOT exist at the literal relative-file-path spelling
// (../../../plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h) - it
// is a CMake configure_file() output living under
// <build-dir>/generated/gte_plugin_abi/ instead. The correct spelling relies
// on gte_plugin_abi's own INTERFACE include directory (brought in via
// target_link_libraries(gte_core PUBLIC gte_plugin_abi), root
// CMakeLists.txt), confirmed against the real, current
// plugins/gte_plugin_abi/CMakeLists.txt before writing this include.
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <windows.h>

#include <cstring>

#include <string>

namespace gte {

namespace {

// Logs EVERY differing field by name (source design doc Section 4.1's own
// example: "built with MSVC 19.38, host is MSVC 19.42") rather than a single
// generic "fingerprint mismatch" line - see PluginHost::TryLoadOnePlugin()'s
// own doc/step-by-step comment (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md,
// Step 3.2, point 3).
std::string DescribeFingerprintMismatch(const GtePluginAbiFingerprint& pluginFp, const GtePluginAbiFingerprint& hostFp)
{
    std::string result;
    auto appendMismatch = [&result](const char* fieldName, const std::string& pluginValue, const std::string& hostValue) {
        if (!result.empty()) {
            result += "; ";
        }
        result += std::string(fieldName) + "(plugin=" + pluginValue + ", host=" + hostValue + ")";
    };

    if (pluginFp.abiContractGeneration != hostFp.abiContractGeneration) {
        appendMismatch("abiContractGeneration", std::to_string(pluginFp.abiContractGeneration), std::to_string(hostFp.abiContractGeneration));
    }
    // compilerId/buildConfig are always null-terminated within their own
    // fixed width - MakeThisBuildsFingerprint() zero-initializes the whole
    // struct first, then copies at most 15 characters (loop bound `i < 15`),
    // leaving byte 15 permanently 0 - safe to build a std::string from the
    // raw char* directly.
    if (std::string(pluginFp.compilerId) != std::string(hostFp.compilerId)) {
        appendMismatch("compilerId", pluginFp.compilerId, hostFp.compilerId);
    }
    if (pluginFp.compilerVersionMajor != hostFp.compilerVersionMajor
        || pluginFp.compilerVersionMinor != hostFp.compilerVersionMinor
        || pluginFp.compilerVersionPatch != hostFp.compilerVersionPatch) {
        appendMismatch("compilerVersion",
            std::to_string(pluginFp.compilerVersionMajor) + "." + std::to_string(pluginFp.compilerVersionMinor) + "." + std::to_string(pluginFp.compilerVersionPatch),
            std::to_string(hostFp.compilerVersionMajor) + "." + std::to_string(hostFp.compilerVersionMinor) + "." + std::to_string(hostFp.compilerVersionPatch));
    }
    if (std::string(pluginFp.buildConfig) != std::string(hostFp.buildConfig)) {
        appendMismatch("buildConfig", pluginFp.buildConfig, hostFp.buildConfig);
    }
    if (pluginFp.pointerSize != hostFp.pointerSize) {
        appendMismatch("pointerSize", std::to_string(pluginFp.pointerSize), std::to_string(hostFp.pointerSize));
    }
    if (pluginFp.sharedRuntimeLinkage != hostFp.sharedRuntimeLinkage) {
        appendMismatch("sharedRuntimeLinkage", std::to_string(pluginFp.sharedRuntimeLinkage), std::to_string(hostFp.sharedRuntimeLinkage));
    }
    return result;
}

// editor-core-separation-4 campaign, PHASE4
// (PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md) - a second,
// independent safety net beyond cmake/MingwRuntime.cmake's own
// gte_apply_plugin_dll_shared_crt_linkage() fix (which stops these files
// from ever being COPIED into plugins/ in the first place for a plugin
// .dll target). This list additionally protects against these exact
// filenames ending up in plugins/ for any OTHER reason (e.g. a developer
// manually copying one there, or a future CMake change reintroducing the
// same mistake this phase fixes) - PluginHost is a load-bearing safety
// boundary, it should not silently regress if the CMake-level fix is ever
// undone by accident.
constexpr const char* kKnownNonPluginFilenames[] = {
    "libstdc++-6.dll",
    "libgcc_s_seh-1.dll",
    "libwinpthread-1.dll",
};

bool IsKnownNonPluginFilename(const std::filesystem::path& fileName)
{
    const std::string name = fileName.string();
    for (const char* known : kKnownNonPluginFilenames) {
        if (_stricmp(name.c_str(), known) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

PluginHost::~PluginHost()
{
    // Reverse load order (last-loaded, first-destroyed) - mirrors ordinary
    // RAII/stack-unwind destruction order convention. Destroy the C++ object
    // living inside the .dll's own code segment BEFORE unmapping that code
    // segment (FreeLibrary), never the reverse.
    for (auto it = m_loadedPlugins.rbegin(); it != m_loadedPlugins.rend(); ++it) {
        if (it->destroyFn != nullptr && it->module != nullptr) {
            it->destroyFn(it->module);
        }
        if (it->moduleHandle != nullptr) {
            FreeLibrary(static_cast<HMODULE>(it->moduleHandle));
        }
    }
}

void PluginHost::LoadPlugins(const std::filesystem::path& pluginsDirectory)
{
    LogSharedCrtRiskWarningOnce();

    if (!std::filesystem::exists(pluginsDirectory)) {
        GTE_LOG_INFO("PluginHost", "no plugins directory found at " + pluginsDirectory.string() + ", skipping");
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(pluginsDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".dll") {
            continue;
        }
        if (IsKnownNonPluginFilename(entry.path().filename())) {
            GTE_LOG_INFO("PluginHost", "Skipping known non-plugin runtime file: " + entry.path().string());
            continue;
        }
        TryLoadOnePlugin(entry.path());
    }
}

// PHASE1 (editor-core-separation-4 campaign) - see PluginHost.h's own doc
// comment on this method's declaration for the full reasoning. Logs ONE
// loud GTE_LOG_WARNING, at most once per PluginHost instance, the first
// time LoadPlugins() runs, if-and-only-if this build's own fingerprint has
// sharedRuntimeLinkage == 0.
void PluginHost::LogSharedCrtRiskWarningOnce()
{
    if (m_sharedCrtRiskWarningLogged) {
        return;
    }
    m_sharedCrtRiskWarningLogged = true;

    const GtePluginAbiFingerprint hostFingerprint = MakeThisBuildsFingerprint();
    if (hostFingerprint.sharedRuntimeLinkage != 0) {
        return; // Genuinely shared-CRT-linked - nothing to warn about.
    }

    GTE_LOG_WARNING("PluginHost",
        "This build was NOT linked with shared/DLL CRT (sharedRuntimeLinkage=0). "
        "A statically-linked host and statically-linked plugin .dll(s) do NOT "
        "share one process-wide heap - allocating on one side of the plugin ABI "
        "boundary and freeing on the other (even indirectly) is undefined "
        "behavior. This is currently a real, unenforced risk on this build - "
        "see plugins/gte_plugin_abi/GtePluginAbiFingerprint.h's "
        "sharedRuntimeLinkage field and docs/conventions/plugin-architecture.md "
        "for the full explanation.");
}

void PluginHost::TryLoadOnePlugin(const std::filesystem::path& dllPath)
{
    const std::string pathStr = dllPath.string();

    // 1. LoadLibraryW().
    HMODULE handle = LoadLibraryW(dllPath.wstring().c_str());
    if (handle == nullptr) {
        GTE_LOG_WARNING("PluginHost", "Failed to LoadLibraryW: " + pathStr);
        return;
    }

    // 2. Resolve the 3 fixed exports by exact, case-sensitive name.
    auto getFingerprintFn = reinterpret_cast<PFN_GTE_GetPluginAbiFingerprint>(
        reinterpret_cast<void*>(GetProcAddress(handle, kGteGetPluginAbiFingerprintExportName)));
    if (getFingerprintFn == nullptr) {
        GTE_LOG_WARNING("PluginHost", pathStr + " is missing export " + kGteGetPluginAbiFingerprintExportName + ", not a valid plugin, skipping");
        FreeLibrary(handle);
        return;
    }

    auto createFn = reinterpret_cast<PFN_GTE_CreatePluginModule>(
        reinterpret_cast<void*>(GetProcAddress(handle, kGteCreatePluginModuleExportName)));
    if (createFn == nullptr) {
        GTE_LOG_WARNING("PluginHost", pathStr + " is missing export " + kGteCreatePluginModuleExportName + ", not a valid plugin, skipping");
        FreeLibrary(handle);
        return;
    }

    auto destroyFn = reinterpret_cast<PFN_GTE_DestroyPluginModule>(
        reinterpret_cast<void*>(GetProcAddress(handle, kGteDestroyPluginModuleExportName)));
    if (destroyFn == nullptr) {
        GTE_LOG_WARNING("PluginHost", pathStr + " is missing export " + kGteDestroyPluginModuleExportName + ", not a valid plugin, skipping");
        FreeLibrary(handle);
        return;
    }

    // 3. Fingerprint check - the single most important check in this whole
    // class. Never skipped, never softened into a warning-only "load
    // anyway."
    const GtePluginAbiFingerprint pluginFingerprint = getFingerprintFn();
    const GtePluginAbiFingerprint hostFingerprint = MakeThisBuildsFingerprint();
    if (!(pluginFingerprint == hostFingerprint)) {
        GTE_LOG_WARNING("PluginHost", pathStr + ": ABI fingerprint mismatch - " + DescribeFingerprintMismatch(pluginFingerprint, hostFingerprint));
        FreeLibrary(handle);
        return;
    }

    // 4. GTE_CreatePluginModule() - nullptr is a legal "I decline to load"
    // signal (source design doc Section 3.1), not an error.
    IPluginModule* module = createFn();
    if (module == nullptr) {
        GTE_LOG_INFO("PluginHost", pathStr + "'s GTE_CreatePluginModule() returned nullptr, plugin declined to load");
        FreeLibrary(handle);
        return;
    }

    // 5. Success.
    GtePluginModuleInfo info;
    module->GetModuleInfo(info);
    // editor-core-separation-4 campaign, PHASE6
    // (PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md) - bounded reads via
    // ReadFixedBuffer() (FixedBufferReader.h), never trusting a plugin's own
    // GetModuleInfo() implementation to have null-terminated these
    // fixed-size char[] buffers correctly.
    GTE_LOG_INFO("PluginHost", "Loaded plugin '" + ReadFixedBuffer(info.name, sizeof(info.name)) + "' v"
        + ReadFixedBuffer(info.version, sizeof(info.version)) + " from " + pathStr);

    LoadedPlugin loaded;
    loaded.moduleHandle = handle;
    loaded.module = module;
    loaded.destroyFn = destroyFn;
    m_loadedPlugins.push_back(loaded);
    m_modules.push_back(module);
}

} // namespace gte
