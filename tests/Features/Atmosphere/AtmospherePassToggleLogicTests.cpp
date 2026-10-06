#include "Features/Atmosphere/AtmospherePassToggleLogic.h"

#include <gtest/gtest.h>

namespace {

TEST(AtmospherePassToggleLogicTest, ReturnsFalseWhenDisabledRegardlessOfUpstreamValidity)
{
    EXPECT_FALSE(gte::ShouldDeclareAtmospherePassThisFrame(/*passEnabledThisFrame=*/false,
        /*allUpstreamHandlesValid=*/true));
    EXPECT_FALSE(gte::ShouldDeclareAtmospherePassThisFrame(false, false));
}

TEST(AtmospherePassToggleLogicTest, ReturnsFalseWhenAnyUpstreamHandleIsInvalidRegardlessOfEnabledState)
{
    EXPECT_FALSE(gte::ShouldDeclareAtmospherePassThisFrame(/*passEnabledThisFrame=*/true,
        /*allUpstreamHandlesValid=*/false));
}

TEST(AtmospherePassToggleLogicTest, ReturnsTrueOnlyWhenEnabledAndEveryUpstreamHandleIsValid)
{
    EXPECT_TRUE(gte::ShouldDeclareAtmospherePassThisFrame(/*passEnabledThisFrame=*/true,
        /*allUpstreamHandlesValid=*/true));
}

} // namespace
