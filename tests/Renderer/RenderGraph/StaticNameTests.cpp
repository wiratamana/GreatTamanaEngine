// Tier-1 tests: StaticName only accepts string literals.

#include "Renderer/RenderGraph/StaticName.h"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>

namespace gte::rg {
namespace {

static_assert(!std::is_constructible_v<StaticName, const char*>);
static_assert(!std::is_constructible_v<StaticName, std::string>);
static_assert(std::is_constructible_v<StaticName, const char (&)[5]>);

TEST(StaticNameTest, ValueIsTheLiteral)
{
    constexpr StaticName name("Pass");
    EXPECT_STREQ(name.value, "Pass");
}

} // namespace
} // namespace gte::rg
