// Tier-2 tests for Core's per-view jitter store. They need a headless Core and skip without one.

#include "Core/Core.h"
#include "Core/IHostServices.h"

#include "../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <exception>
#include <limits>
#include <memory>
#include <string_view>

namespace gte {
namespace {

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel, std::string_view) override { }
};

class ViewJitterStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        try {
            core = std::make_unique<Core>(surface, services);
        } catch (const std::exception&) {
            GTEST_SKIP() << "Core construction needs headless Vulkan, which this machine lacks.";
        }
    }

    HeadlessSurfaceProvider surface;
    NoopHostServices services;
    std::unique_ptr<Core> core;
};

TEST_F(ViewJitterStoreTest, PrimaryJitterIsStoredAndSecondaryStaysZero)
{
    const Vec2 jitter{ 0.001f, -0.002f };
    EXPECT_TRUE(core->SetViewJitter(rg::ViewScope::Primary, jitter));
    EXPECT_EQ(core->ViewJitterNdc(rg::ViewScope::Primary), jitter);
    EXPECT_EQ(core->ViewJitterNdc(rg::ViewScope::Secondary), Vec2::Zero());
}

TEST_F(ViewJitterStoreTest, SharedScopeIsRefused)
{
    EXPECT_FALSE(core->SetViewJitter(rg::ViewScope::Shared, Vec2{ 0.001f, 0.001f }));
    EXPECT_EQ(core->ViewJitterNdc(rg::ViewScope::Shared), Vec2::Zero());
}

TEST_F(ViewJitterStoreTest, InvalidValuesAreRefusedAndKeepTheOldJitter)
{
    const Vec2 jitter{ 0.003f, 0.004f };
    ASSERT_TRUE(core->SetViewJitter(rg::ViewScope::Primary, jitter));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(core->SetViewJitter(rg::ViewScope::Primary, Vec2{ nan, 0.0f }));
    EXPECT_FALSE(core->SetViewJitter(rg::ViewScope::Primary, Vec2{ 0.6f, 0.0f }));
    EXPECT_EQ(core->ViewJitterNdc(rg::ViewScope::Primary), jitter);
}

} // namespace
} // namespace gte
