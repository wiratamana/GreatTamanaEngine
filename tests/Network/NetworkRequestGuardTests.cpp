// Tier 1 tests for the browser request guard of destructive routes.

#include "Network/NetworkRequestGuard.h"

#include <gtest/gtest.h>

namespace {

using namespace gte::Network;

constexpr int kPort = 8080;

TEST(NetworkRequestGuardTests, PlainClientWithNoHeadersPasses)
{
    EXPECT_FALSE(RejectBrowserRequest({}, kPort));
}

TEST(NetworkRequestGuardTests, SecFetchSiteMustBeNoneOrSameOrigin)
{
    EXPECT_FALSE(RejectBrowserRequest({ .secFetchSite = "none" }, kPort));
    EXPECT_FALSE(RejectBrowserRequest({ .secFetchSite = "same-origin" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .secFetchSite = "cross-site" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .secFetchSite = "same-site" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .secFetchSite = "" }, kPort));
}

TEST(NetworkRequestGuardTests, OriginHostMustBeLoopback)
{
    EXPECT_FALSE(RejectBrowserRequest({ .origin = "http://127.0.0.1:8080" }, kPort));
    EXPECT_FALSE(RejectBrowserRequest({ .origin = "http://localhost:3000" }, kPort));
    EXPECT_FALSE(RejectBrowserRequest({ .origin = "http://LOCALHOST" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .origin = "https://example.com" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .origin = "http://127.0.0.1.evil.com:8080" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .origin = "null" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .origin = "" }, kPort));
}

TEST(NetworkRequestGuardTests, HostMustBeLoopbackWithThePort)
{
    EXPECT_FALSE(RejectBrowserRequest({ .host = "127.0.0.1:8080" }, kPort));
    EXPECT_FALSE(RejectBrowserRequest({ .host = "localhost:8080" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .host = "127.0.0.1:9090" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .host = "127.0.0.1" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .host = "evil.com:8080" }, kPort));
    EXPECT_TRUE(RejectBrowserRequest({ .host = "" }, kPort));
}

TEST(NetworkRequestGuardTests, OneBadHeaderRejectsEvenWhenOthersAreFine)
{
    EXPECT_TRUE(RejectBrowserRequest(
        { .secFetchSite = "same-origin", .origin = "http://localhost:8080", .host = "evil.com:8080" }, kPort));
    EXPECT_FALSE(RejectBrowserRequest(
        { .secFetchSite = "same-origin", .origin = "http://localhost:8080", .host = "localhost:8080" }, kPort));
}

} // namespace
