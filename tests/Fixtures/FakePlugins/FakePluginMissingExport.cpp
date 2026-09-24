// tests/Fixtures/FakePlugins/FakePluginMissingExport.cpp
//
// editor-core-separation-4 campaign, PHASE8 - deliberately exports ONLY
// GTE_GetPluginAbiFingerprint and GTE_DestroyPluginModule, OMITTING
// GTE_CreatePluginModule, proving PluginHost::TryLoadOnePlugin()'s "missing
// export" skip path (omitting exactly one of the 3 required exports is
// sufficient to prove this - see PHASE8's own phase file).

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

extern "C" {

__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}

// GTE_CreatePluginModule deliberately NOT exported.

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule*) { }

} // extern "C"
