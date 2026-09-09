#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>

namespace {

TEST(NetworkRoutesTests, HandleHelloWorldReturnsExactContractedString)
{
    EXPECT_EQ(gte::Network::HandleHelloWorld(), "hello world");
}

} // namespace
