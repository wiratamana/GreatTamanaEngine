// Tier 1 tests for the fault injection query, replies and consume-once counters.

#include "Network/NetworkFaultRoutes.h"

#include <gtest/gtest.h>

#if GTE_ENABLE_NETWORK

namespace {

using namespace gte;
using namespace gte::Network;

// Clears every armed counter before and after a test: the counters are process-wide.
class FaultRoutesTests : public ::testing::Test {
protected:
    void SetUp() override { Clear(); }
    void TearDown() override { Clear(); }

    static void Clear()
    {
        for (std::size_t i = 0; i < kFaultKindCount; ++i) {
            FaultInjection::Instance().Arm(static_cast<FaultKind>(i), 0);
        }
    }
};

TEST_F(FaultRoutesTests, ParsesEveryKindName)
{
    for (std::size_t i = 0; i < kFaultKindCount; ++i) {
        const FaultKind kind = static_cast<FaultKind>(i);
        const ParsedFaultQuery parsed = ParseFaultQuery(std::string(FaultKindName(kind)), "2");
        ASSERT_TRUE(parsed.valid);
        EXPECT_EQ(parsed.kind, kind);
        EXPECT_EQ(parsed.count, 2u);
    }
}

TEST_F(FaultRoutesTests, CountDefaultsToOne)
{
    const ParsedFaultQuery parsed = ParseFaultQuery("device_lost", "");
    ASSERT_TRUE(parsed.valid);
    EXPECT_EQ(parsed.count, 1u);
}

TEST_F(FaultRoutesTests, NoArgumentsMeansList)
{
    const ParsedFaultQuery parsed = ParseFaultQuery("", "");
    EXPECT_TRUE(parsed.valid);
    EXPECT_TRUE(parsed.listOnly);
}

TEST_F(FaultRoutesTests, RejectsBadKindAndBadCounts)
{
    EXPECT_FALSE(ParseFaultQuery("explode", "1").valid);
    EXPECT_FALSE(ParseFaultQuery("", "3").valid);
    EXPECT_FALSE(ParseFaultQuery("device_lost", "0").valid);
    EXPECT_FALSE(ParseFaultQuery("device_lost", "1001").valid);
    EXPECT_FALSE(ParseFaultQuery("device_lost", "99999999999999").valid);
    EXPECT_FALSE(ParseFaultQuery("device_lost", "-1").valid);
    EXPECT_FALSE(ParseFaultQuery("device_lost", "2x").valid);
    EXPECT_FALSE(ParseFaultQuery("device_lost", " 2").valid);
    EXPECT_TRUE(ParseFaultQuery("device_lost", "1000").valid);
}

TEST_F(FaultRoutesTests, ConsumeFiresOncePerArmedCount)
{
    FaultInjection& faults = FaultInjection::Instance();
    EXPECT_FALSE(faults.Consume(FaultKind::PresentThrow));
    faults.Arm(FaultKind::PresentThrow, 2);
    EXPECT_FALSE(faults.Consume(FaultKind::OffscreenThrow));
    EXPECT_TRUE(faults.Consume(FaultKind::PresentThrow));
    EXPECT_TRUE(faults.Consume(FaultKind::PresentThrow));
    EXPECT_FALSE(faults.Consume(FaultKind::PresentThrow));
}

TEST_F(FaultRoutesTests, ArmReplyEchoesKindAndCountAndArms)
{
    const FaultReply reply = HandleFaultRequest("offscreen_throw", "3");
    EXPECT_EQ(reply.status, 200);
    EXPECT_NE(reply.body.find("\"armed\":\"offscreen_throw\""), std::string::npos);
    EXPECT_NE(reply.body.find("\"count\":3"), std::string::npos);
    EXPECT_EQ(FaultInjection::Instance().Snapshot()[static_cast<std::size_t>(FaultKind::OffscreenThrow)], 3u);
}

TEST_F(FaultRoutesTests, ListReplyShowsEveryCounter)
{
    FaultInjection::Instance().Arm(FaultKind::MainThreadStall, 4);
    const FaultReply reply = HandleFaultRequest("", "");
    EXPECT_EQ(reply.status, 200);
    EXPECT_NE(reply.body.find("\"main_thread_stall\":4"), std::string::npos);
    EXPECT_NE(reply.body.find("\"device_lost\":0"), std::string::npos);
}

TEST_F(FaultRoutesTests, BadRequestAnswers400AndArmsNothing)
{
    const FaultReply reply = HandleFaultRequest("explode", "1");
    EXPECT_EQ(reply.status, 400);
    for (const std::uint32_t armed : FaultInjection::Instance().Snapshot()) {
        EXPECT_EQ(armed, 0u);
    }
}

} // namespace

#endif
