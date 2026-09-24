#include "Core/Plugins/PluginRenderFeatureDiagnostics.h"
#include "../../plugins/gte_plugin_abi/IPluginModule.h"
#include "../../plugins/gte_plugin_abi/GtePluginModuleInfo.h"
#include "../../plugins/gte_plugin_abi/IRenderFeatureModule.h"
#include "../../plugins/gte_plugin_abi/IPluginRenderPassBuilder.h"

#include <gtest/gtest.h>

#include <cstring>

namespace gte {
namespace {

class FakeRenderFeature final : public IRenderFeatureModule_v1 {
public:
    void AddRenderGraphPasses(IPluginRenderPassBuilder&) override { }
};

class FakeModuleWithRenderFeature final : public IPluginModule {
public:
    void* QueryCapability(const char* nameAndVersion) override
    {
        if (std::strcmp(nameAndVersion, kIRenderFeatureModule_v1_Name) == 0) {
            return &m_feature;
        }
        return nullptr;
    }
    void GetModuleInfo(GtePluginModuleInfo&) const override { }

private:
    FakeRenderFeature m_feature;
};

class FakeModuleWithoutRenderFeature final : public IPluginModule {
public:
    void* QueryCapability(const char*) override { return nullptr; }
    void GetModuleInfo(GtePluginModuleInfo&) const override { }
};

TEST(PluginRenderFeatureDiagnosticsTest, CountModulesImplementingRenderFeature_EmptyVectorReturnsZero)
{
    EXPECT_EQ(CountModulesImplementingRenderFeature({}), 0);
}

TEST(PluginRenderFeatureDiagnosticsTest, CountModulesImplementingRenderFeature_CountsOnlyModulesThatImplementIt)
{
    FakeModuleWithRenderFeature withFeatureA;
    FakeModuleWithRenderFeature withFeatureB;
    FakeModuleWithoutRenderFeature without;

    const std::vector<IPluginModule*> modules = { &withFeatureA, &without, &withFeatureB };

    EXPECT_EQ(CountModulesImplementingRenderFeature(modules), 2);
}

TEST(PluginRenderFeatureDiagnosticsTest, CountModulesImplementingRenderFeature_ZeroWhenNoneImplementIt)
{
    FakeModuleWithoutRenderFeature withoutA;
    FakeModuleWithoutRenderFeature withoutB;

    const std::vector<IPluginModule*> modules = { &withoutA, &withoutB };

    EXPECT_EQ(CountModulesImplementingRenderFeature(modules), 0);
}

} // namespace
} // namespace gte
