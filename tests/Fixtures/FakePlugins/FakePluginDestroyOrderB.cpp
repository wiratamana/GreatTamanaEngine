// tests/Fixtures/FakePlugins/FakePluginDestroyOrderB.cpp
//
// editor-core-separation-4 campaign, PHASE8 - IDENTICAL to
// FakePluginDestroyOrderA.cpp except every "A" becomes "B" - see that
// file's own header comment for the full reasoning.

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <cstdio>
#include <cstdlib>

namespace {

void AppendMarker(const char* line)
{
    const char* path = std::getenv("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE");
    if (path == nullptr) {
        return;
    }
    if (FILE* f = std::fopen(path, "a")) {
        std::fprintf(f, "%s\n", line);
        std::fclose(f);
    }
}

class FakeModuleB final : public gte::IPluginModule {
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
    AppendMarker("CREATE:B");
    return new FakeModuleB();
}

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    AppendMarker("DESTROY:B");
    delete module;
}

} // extern "C"
