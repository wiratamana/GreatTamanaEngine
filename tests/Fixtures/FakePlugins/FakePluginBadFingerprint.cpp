// tests/Fixtures/FakePlugins/FakePluginBadFingerprint.cpp
//
// editor-core-separation-4 campaign, PHASE8 - exports all 3 required
// exports, but GTE_GetPluginAbiFingerprint() returns a deliberately
// corrupted fingerprint (one field intentionally wrong), proving
// PluginHost::TryLoadOnePlugin()'s fingerprint-mismatch skip path.
// FakeModule/GTE_CreatePluginModule() here are never actually reached at
// runtime - the fingerprint check runs BEFORE GTE_CreatePluginModule() is
// ever called - so this fixture's own create/destroy pair only needs to
// compile correctly, matching the real ABI contract's required export
// signatures.

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

namespace {
class FakeModule final : public gte::IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; }
    void GetModuleInfo(gte::GtePluginModuleInfo&) const override { }
};
} // namespace

extern "C" {

__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    gte::GtePluginAbiFingerprint fp = gte::MakeThisBuildsFingerprint();
    fp.abiContractGeneration = 999999; // deliberately, unambiguously wrong
    return fp;
}

__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return new FakeModule(); }

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module) { delete module; }

} // extern "C"
