// tests/Fixtures/FakePlugins/FakePluginDeclinesToLoad.cpp
//
// editor-core-separation-4 campaign, PHASE8 - exports all 3 required
// exports with a CORRECT fingerprint, but GTE_CreatePluginModule() always
// returns nullptr, a legal "I decline to load" signal (source design doc
// Section 3.1) - proving PluginHost::TryLoadOnePlugin()'s decline-to-load
// skip path.

#include "../../../plugins/gte_plugin_abi/IPluginModule.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"

extern "C" {

__declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint()
{
    return gte::MakeThisBuildsFingerprint();
}

__declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return nullptr; }

__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule*) { }

} // extern "C"
