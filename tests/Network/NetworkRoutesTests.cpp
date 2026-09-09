#include "Network/NetworkRoutes.h"

#include <gtest/gtest.h>

namespace {

using gte::Network::CaptureResponseFormat;
using gte::Network::ResolveCaptureResponseFormat;

TEST(NetworkRoutesTests, HandleHelloWorldReturnsExactContractedString)
{
    EXPECT_EQ(gte::Network::HandleHelloWorld(), "hello world");
}

// network-impl-2 campaign, Phase 3
// (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - table-driven
// coverage of every precedence rule ResolveCaptureResponseFormat() documents
// in NetworkRoutes.h.

struct ResolveCaptureResponseFormatCase {
    std::string queryFormat;
    std::string acceptHeader;
    CaptureResponseFormat expected;
};

class ResolveCaptureResponseFormatTest : public ::testing::TestWithParam<ResolveCaptureResponseFormatCase> {};

TEST_P(ResolveCaptureResponseFormatTest, MatchesLockedPrecedenceRules)
{
    const ResolveCaptureResponseFormatCase& testCase = GetParam();
    EXPECT_EQ(ResolveCaptureResponseFormat(testCase.queryFormat, testCase.acceptHeader), testCase.expected);
}

INSTANTIATE_TEST_SUITE_P(NetworkRoutesTests, ResolveCaptureResponseFormatTest,
    ::testing::Values(
        // ?format=png always wins, regardless of Accept.
        ResolveCaptureResponseFormatCase{ "png", "", CaptureResponseFormat::RawPng },
        ResolveCaptureResponseFormatCase{ "png", "application/json", CaptureResponseFormat::RawPng },
        // ?format=base64 / ?format=json always win, regardless of Accept.
        ResolveCaptureResponseFormatCase{ "base64", "", CaptureResponseFormat::JsonBase64 },
        ResolveCaptureResponseFormatCase{ "json", "text/plain", CaptureResponseFormat::JsonBase64 },
        // An unrecognized ?format= value is not an error - falls back to RawPng.
        ResolveCaptureResponseFormatCase{ "bogus", "application/json", CaptureResponseFormat::RawPng },
        // No ?format= at all - Accept decides.
        ResolveCaptureResponseFormatCase{ "", "application/json", CaptureResponseFormat::JsonBase64 },
        ResolveCaptureResponseFormatCase{ "", "text/html,application/json;q=0.9", CaptureResponseFormat::JsonBase64 },
        ResolveCaptureResponseFormatCase{ "", "text/plain", CaptureResponseFormat::RawPng },
        ResolveCaptureResponseFormatCase{ "", "", CaptureResponseFormat::RawPng }));

TEST(NetworkRoutesTests, BuildCaptureJsonBodyProducesExactExpectedShape)
{
    const std::string body = gte::Network::BuildCaptureJsonBody(64, 32, "QUJD");
    EXPECT_EQ(body, "{\"width\":64,\"height\":32,\"format\":\"png\",\"data_base64\":\"QUJD\"}");
}

TEST(NetworkRoutesTests, BuildCaptureJsonBodyHandlesEmptyBase64)
{
    const std::string body = gte::Network::BuildCaptureJsonBody(0, 0, "");
    EXPECT_EQ(body, "{\"width\":0,\"height\":0,\"format\":\"png\",\"data_base64\":\"\"}");
}

} // namespace
