// plugins/demo_render_feature_second/RenderFeaturePlugin.cpp
//
// editor-core-separation-4 campaign, PHASE5
// (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
// SECOND, genuinely independent throwaway demo plugin implementing
// IRenderFeatureModule_v1, mirroring plugins/demo_render_feature/
// RenderFeaturePlugin.cpp's exact shape byte for byte, only changing the
// class/info names and the pass name string. Deliberately clears to the
// exact SAME solid magenta color the first demo already uses, so the
// existing, extensively-documented "solid magenta Game/Scene View" visual
// baseline stays visually unchanged with this plugin added - this exercises
// the "2+ plugins implementing IRenderFeatureModule_v1" path for real,
// which today silently overwrites each other's render output (see
// docs/conventions/plugin-architecture.md and Core::LoadPlugins()'s own
// new GTE_LOG_WARNING for the real, current behavior).

#include "../gte_plugin_abi/IPluginModule.h"
#include "../gte_plugin_abi/GtePluginModuleInfo.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
#include "../gte_plugin_abi/IRenderFeatureModule.h"
#include "../gte_plugin_abi/IPluginRenderPassBuilder.h"

#include <cstring>

namespace gte {
namespace {

class DemoRenderFeatureSecond final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) override
    {
        builder.AddFullscreenClearPass("DemoRenderFeatureSecondPlugin_Clear", 1.0f, 0.0f, 1.0f, 1.0f);
    }
};

class DemoRenderFeatureSecondPluginModule final : public IPluginModule {
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
        std::strncpy(outInfo.name, "DemoRenderFeaturePluginSecond", sizeof(outInfo.name) - 1);
        std::strncpy(outInfo.version, "1.0.0", sizeof(outInfo.version) - 1);
        std::strncpy(outInfo.description,
            "editor-core-separation-4 PHASE5 proof - a SECOND plugin implementing "
            "IRenderFeatureModule_v1, clearing to the same magenta as the first, "
            "to exercise the 2-plugin path.",
            sizeof(outInfo.description) - 1);
    }

private:
    DemoRenderFeatureSecond m_feature;
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
    return new gte::DemoRenderFeatureSecondPluginModule();
}
__declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module)
{
    delete module;
}
}
