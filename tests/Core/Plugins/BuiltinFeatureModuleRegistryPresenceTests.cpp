// Permanent presence check: proves BuiltinFeatureModuleRegistry's static
// self-registration mechanism actually fires when a built-in feature's
// folder is present - an acid test (folder absent) only proves absence,
// never that registration would have worked while the folder DOES exist.
//
// Reuses the EXACT HeadlessSurfaceProvider + NoopHostServices + real Core
// fixture pattern already proven elsewhere in this test suite (see
// tests/Core/RegisterProjectRenderFeatureApiTests.cpp) - GTEST_SKIP()-ing
// identically if this machine's Vulkan driver/loader doesn't support
// VK_EXT_headless_surface.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Core/Plugins/BuiltinFeatureModuleRegistry.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace gte {
namespace {

// A trivial, no-op IHostServices - Core's constructor requires a real
// IHostServices&, but this test never needs to observe anything logged
// through it.
class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override { }
};

std::unique_ptr<Core> TryMakeHeadlessCore(HeadlessSurfaceProvider& surfaceProvider, NoopHostServices& hostServices)
{
    try {
        return std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception&) {
        return nullptr;
    }
}

} // namespace

TEST(BuiltinFeatureModuleRegistryPresenceTest, AtLeastOneBuiltinFeatureModuleSelfRegistersAndCreates)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);
    if (core == nullptr) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment).";
    }

    std::vector<std::unique_ptr<IEngineFeatureModule>> modules =
        BuiltinFeatureModuleRegistry::Instance().CreateAll(*core);

    ASSERT_FALSE(modules.empty());

    bool foundAtmosphere = false;
    for (const std::unique_ptr<IEngineFeatureModule>& module : modules) {
        ASSERT_NE(module, nullptr);
        if (std::string_view(module->ModuleName()) == "Atmosphere") {
            foundAtmosphere = true;
        }
    }
    EXPECT_TRUE(foundAtmosphere);
}

} // namespace gte
