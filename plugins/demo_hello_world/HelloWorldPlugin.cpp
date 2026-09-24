// plugins/demo_hello_world/HelloWorldPlugin.cpp
//
// PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.5 - Milestone
// 0's own handshake proof: implements IPluginModule::GetModuleInfo() and
// QueryCapability() (returning nullptr unconditionally - zero capabilities),
// nothing else. This plugin is deliberately tiny/throwaway-quality, per
// PHASE0_MASTER_STRATEGY.md's own Step 1 - it exists to prove the ABI
// boundary works, not to demonstrate a real feature.

#include "../gte_plugin_abi/IPluginModule.h"
#include "../gte_plugin_abi/GtePluginModuleInfo.h"
// PHASE1_COMPLETION_REPORT.md's own "Minor note" - the generated fingerprint
// header does NOT exist at the literal relative-file-path spelling used by
// this phase file's own sketch (../gte_plugin_abi/GtePluginAbiFingerprintGenerated.h)
// - it is a CMake configure_file() output living under
// <build-dir>/generated/gte_plugin_abi/ instead. The correct spelling relies
// on gte_plugin_abi's own INTERFACE include directory (brought in via
// target_link_libraries(demo_hello_world PRIVATE gte_plugin_abi), this
// folder's own CMakeLists.txt).
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

#include <cstring>

namespace gte {

namespace {
class HelloWorldPluginModule final : public IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; } // implements zero capabilities - Milestone 0 proves ONLY the handshake itself
    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override
    {
        std::strncpy(outInfo.name, "HelloWorldPlugin", sizeof(outInfo.name) - 1);
        std::strncpy(outInfo.version, "1.0.0", sizeof(outInfo.version) - 1);
        std::strncpy(outInfo.description, "Milestone 0 handshake proof - implements zero capabilities.", sizeof(outInfo.description) - 1);
    }
};
} // namespace

} // namespace gte

extern "C" {

__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}

__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule()
{
    return new gte::HelloWorldPluginModule();
}

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    delete module;
}

} // extern "C"
