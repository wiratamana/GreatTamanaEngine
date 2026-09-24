// tests/Fixtures/FakePlugins/FakePluginDestroyOrderA.cpp
//
// editor-core-separation-4 campaign, PHASE8 - a correct, successfully-
// loading fixture that appends a plain-text line to a well-known temp
// marker file on EVERY GTE_CreatePluginModule()/GTE_DestroyPluginModule()
// call - this is how PluginHostFailurePathTests.cpp proves REVERSE load
// order without depending on std::filesystem::directory_iterator's own
// (unspecified-by-the-standard) enumeration order.
// FakePluginDestroyOrderB.cpp is IDENTICAL except every "A" below becomes
// "B" - kept as two separate, real files, not a templated/shared one,
// mirroring this codebase's own existing demo-plugin-per-folder convention.

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <cstdio>
#include <cstdlib>

namespace {

void AppendMarker(const char* line)
{
    // GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE is set by the TEST process
    // (_putenv() - see PluginHostFailurePathTests.cpp for exactly why)
    // BEFORE it constructs the PluginHost that loads this fixture, and
    // BEFORE that call reaches LoadLibraryW() on either fixture .dll - a
    // plain OS environment variable is the simplest way to hand this
    // fixture a file path without growing the real gte_plugin_abi ABI
    // surface just for a test fixture's own internal bookkeeping (this
    // file is NEVER loaded by the real, production PluginHost/Editor - it
    // lives only under tests/Fixtures/). This works correctly across the
    // .dll boundary specifically BECAUSE of that ordering: _putenv()
    // writes through to the one real, process-wide Win32 environment
    // block (not a private per-module cache), and each fixture .dll's own
    // statically-linked CRT populates its own getenv()-backing environ
    // array by reading that SAME process-wide block at its own
    // LoadLibraryW()/DllMain(DLL_PROCESS_ATTACH) time - which only happens
    // once PluginHost::LoadPlugins() actually loads this .dll, strictly
    // AFTER the test process's own _putenv() call already ran. Never
    // reorder the test's own _putenv()-then-LoadPlugins() sequence -
    // reversing it would silently break this technique.
    const char* path = std::getenv("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE");
    if (path == nullptr) {
        return;
    }
    if (FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "%s\n", line);
        std::fclose(f);
    }
}

class FakeModuleA final : public gte::IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; }
    void GetModuleInfo(gte::GtePluginModuleInfo&) const override { }
};

} // namespace

extern "C" {

__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}

__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule()
{
    AppendMarker("CREATE:A");
    return new FakeModuleA();
}

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    AppendMarker("DESTROY:A");
    delete module;
}

} // extern "C"
