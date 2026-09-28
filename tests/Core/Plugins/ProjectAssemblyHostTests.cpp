// Tier-1 tests for src/Core/Plugins/ProjectAssemblyHost.h/.cpp's new
// UnloadProjectAssembly()/GetLoadedAssemblyFileNames() additions
// (editor-core-separation-13 campaign, Project Assembly Hot Reload plan,
// BIG-STEP 2, PHASE4 -
// PHASE4_PROJECT_ASSEMBLY_HOST_UNLOAD_GPU_SAFETY_AND_BINARY_BACKUP.md,
// section 3.6).
//
// A REAL .dll load requires a real, built Project Assembly on disk (heavier
// than a pure unit test should need) - these tests scope to what's testable
// WITHOUT a real .dll: a freshly-constructed, never-loaded
// ProjectAssemblyHost. Test 2 needs a minimal/headless Core AND Renderer -
// reuses CoreHeadlessConstructionTests.cpp's own exact fixture precedent
// (HeadlessSurfaceProvider + a trivial NoopHostServices), GTEST_SKIP()-ing
// identically if this machine's Vulkan driver/loader doesn't support
// VK_EXT_headless_surface (editor-core-separation-1 campaign, PHASE18).

#include "Core/Plugins/ProjectAssemblyHost.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace gte {
namespace {

// A trivial, no-op IHostServices - mirrors CoreHeadlessConstructionTests.cpp's
// own identical NoopHostServices exactly (Core's constructor requires a real
// IHostServices&, but this test never needs to observe anything logged
// through it).
class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override {}
};

} // namespace

TEST(ProjectAssemblyHostTest, GetLoadedAssemblyFileNamesOnAFreshlyConstructedHostIsEmpty)
{
    ProjectAssemblyHost host;
    EXPECT_TRUE(host.GetLoadedAssemblyFileNames().empty());
    EXPECT_EQ(host.LoadedAssemblyCount(), 0u);
}

TEST(ProjectAssemblyHostTest, UnloadProjectAssemblyOnANeverLoadedHostIsASafeNoOp)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;

    std::unique_ptr<Core> core;
    try {
        core = std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR (Renderer's constructor eagerly "
                        "builds a full Vulkan instance/device/swapchain against it, unconditionally - see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment) - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface, the only mechanism "
                        "this fixture uses to obtain a valid surface without a real OS window/SDL. Real "
                        "failure: "
                     << e.what();
    }
    ASSERT_NE(core, nullptr);

    ProjectAssemblyHost host;
    // Never loaded, never recorded anywhere - must not crash, must not
    // throw, only logs at INFO level (see UnloadProjectAssembly()'s own doc
    // comment) - this is the no-matching-entries early-return path, which
    // never even needs to call renderer.WaitForGpuIdle().
    host.UnloadProjectAssembly("NeverLoaded", *core, core->GetRenderer());

    EXPECT_TRUE(host.GetLoadedAssemblyFileNames().empty());
}

} // namespace gte
