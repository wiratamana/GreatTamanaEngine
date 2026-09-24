// plugins/demo_render_feature/RenderFeaturePlugin.cpp
//
// PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md - Milestone 1's own throwaway
// proof: implements IRenderFeatureModule_v1, contributing exactly one
// render-graph pass that clears the Game/Scene View to solid magenta - a
// distinctive, unmistakable color no real production pass in this engine
// uses today (confirmed via search_in_dir on every existing clear-color
// constant before picking this - kGameClearColor is (20,20,30)/255, nothing
// close to solid magenta), so this phase's own visual smoke test can never
// be confused with a real rendering bug or a pre-existing pass.

#include "../gte_plugin_abi/IPluginModule.h"
#include "../gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder.h"

#include <cstring>

namespace gte {
namespace {

class DemoRenderFeature final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) override
    {
        builder.AddFullscreenClearPass("DemoRenderFeaturePlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f);
    }
};

class DemoRenderFeaturePluginModule final : public IPluginModule {
public:
    void* QueryCapability(const char* nameAndVersion) override
    {
        if (std::strcmp(nameAndVersion, kIRenderFeatureModule_v1_Name) == 0) {
            return static_cast<IRenderFeatureModule_v1*>(&m_feature);
        }
        return nullptr;
    }
    void GetModuleInfo(GtePluginModuleInfo& outInfo) const override
    {
        std::strncpy(outInfo.name, "DemoRenderFeaturePlugin", sizeof(outInfo.name) - 1);
        std::strncpy(outInfo.version, "1.0.0", sizeof(outInfo.version) - 1);
        std::strncpy(outInfo.description, "Milestone 1 proof - clears the Game/Scene View to solid magenta.",
            sizeof(outInfo.description) - 1);
    }

private:
    DemoRenderFeature m_feature;
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
    return new gte::DemoRenderFeaturePluginModule();
}
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    delete module;
}
}
