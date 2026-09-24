// tools/ci/gte_plugin_abi_handshake_probe/main.cpp
//
// editor-core-separation-3 campaign, PHASE2
// (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.6) - a tiny,
// PERMANENT, checked-in, standalone (NON-gte_core/gte_editor) program that
// mechanically re-proves the whole plugin ABI handshake works end-to-end
// OUTSIDE GreatTamanaEditor.exe entirely: LoadLibraryW()s a real,
// already-built demo_hello_world.dll, checks its ABI fingerprint, calls
// GetModuleInfo(), calls the destroy function, FreeLibrary()s, and reports
// pass/fail via plain std::printf/return code (PHASE0_MASTER_STRATEGY.md's
// Universal Rule 5's own explicit exception for a standalone CI-probe
// main.cpp living outside gte_core.a/gte_editor.a).
//
// #includes ONLY plugins/gte_plugin_abi/'s own headers - zero gte_core
// dependency, proving Milestone 0's own "prove the ABI boundary itself works
// before any real capability exists" claim in complete isolation.

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "../../../plugins/gte_plugin_abi/PluginExports.h"
#include "../../../plugins/gte_plugin_abi/GtePluginAbiFingerprint.h"
// PHASE1_COMPLETION_REPORT.md's own "Minor note" - see
// src/Core/Plugins/PluginHost.cpp's identical include for the full
// explanation of why this ONE header (the CMake configure_file() OUTPUT)
// must be spelled relying on the gte_plugin_abi INTERFACE include directory
// instead of a literal relative source-tree path.
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {

// This probe's own known build-tree layout (see this folder's own
// CMakeLists.txt): both this executable and demo_hello_world.dll are built
// by the SAME nested, GTE_CORE_STANDALONE_PROBE_ONLY=ON inner configure -
// this .exe lands directly in that inner build's own CMAKE_BINARY_DIR (no
// RUNTIME_OUTPUT_DIRECTORY override - mirrors GreatTamanaEditor.exe's own
// plain-add_executable() precedent, confirmed during this phase's own
// investigation), and demo_hello_world.dll is copied into
// "<inner-build-dir>/plugins/" (GTE_PLUGIN_RUNTIME_OUTPUT_DIR's own
// GTE_CORE_STANDALONE_PROBE_ONLY=ON branch, root CMakeLists.txt) - i.e.
// directly next to this .exe's own directory, under a "plugins" subfolder.
std::wstring ResolveDemoHelloWorldDllPath()
{
    wchar_t exePathBuffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exePathBuffer, MAX_PATH);
    std::wstring exePath(exePathBuffer, length);

    const std::size_t lastSlash = exePath.find_last_of(L"\\/");
    const std::wstring exeDirectory = (lastSlash != std::wstring::npos) ? exePath.substr(0, lastSlash) : L".";

    return exeDirectory + L"\\plugins\\demo_hello_world.dll";
}

std::string DescribeFingerprintMismatch(const gte::GtePluginAbiFingerprint& pluginFp, const gte::GtePluginAbiFingerprint& hostFp)
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

} // namespace

int main()
{
    const std::wstring dllPath = ResolveDemoHelloWorldDllPath();

    HMODULE handle = LoadLibraryW(dllPath.c_str());
    if (handle == nullptr) {
        std::fprintf(stderr, "FAIL: LoadLibraryW failed for demo_hello_world.dll (GetLastError=%lu)\n", GetLastError());
        return 1;
    }

    auto getFingerprintFn = reinterpret_cast<gte::PFN_GTE_GetPluginAbiFingerprint>(
        reinterpret_cast<void*>(GetProcAddress(handle, gte::kGteGetPluginAbiFingerprintExportName)));
    auto createFn = reinterpret_cast<gte::PFN_GTE_CreatePluginModule>(
        reinterpret_cast<void*>(GetProcAddress(handle, gte::kGteCreatePluginModuleExportName)));
    auto destroyFn = reinterpret_cast<gte::PFN_GTE_DestroyPluginModule>(
        reinterpret_cast<void*>(GetProcAddress(handle, gte::kGteDestroyPluginModuleExportName)));

    if (getFingerprintFn == nullptr || createFn == nullptr || destroyFn == nullptr) {
        std::fprintf(stderr, "FAIL: demo_hello_world.dll is missing at least one of the 3 fixed exports\n");
        FreeLibrary(handle);
        return 1;
    }

    const gte::GtePluginAbiFingerprint pluginFingerprint = getFingerprintFn();
    const gte::GtePluginAbiFingerprint hostFingerprint = gte::MakeThisBuildsFingerprint();
    if (!(pluginFingerprint == hostFingerprint)) {
        std::fprintf(stderr, "FAIL: ABI fingerprint mismatch - %s\n", DescribeFingerprintMismatch(pluginFingerprint, hostFingerprint).c_str());
        FreeLibrary(handle);
        return 1;
    }

    gte::IPluginModule* module = createFn();
    if (module == nullptr) {
        std::fprintf(stderr, "FAIL: GTE_CreatePluginModule() returned nullptr\n");
        FreeLibrary(handle);
        return 1;
    }

    gte::GtePluginModuleInfo info;
    module->GetModuleInfo(info);
    std::printf("OK: loaded plugin '%s' v%s - %s\n", info.name, info.version, info.description);

    destroyFn(module);
    FreeLibrary(handle);

    return 0;
}
