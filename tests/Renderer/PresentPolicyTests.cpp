// Tier 1: pure present-mode choice and policy names.
#include "Renderer/PresentPolicy.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(PresentPolicyTest, CappedAlwaysChoosesFifo)
{
    for (const bool mailbox : { false, true }) {
        for (const bool immediate : { false, true }) {
            EXPECT_EQ(ChoosePresentMode(PresentPolicy::Capped, { mailbox, immediate }), PresentMode::Fifo);
        }
    }
}

TEST(PresentPolicyTest, UncappedPrefersMailboxThenImmediateThenFifo)
{
    EXPECT_EQ(ChoosePresentMode(PresentPolicy::Uncapped, { true, true }), PresentMode::Mailbox);
    EXPECT_EQ(ChoosePresentMode(PresentPolicy::Uncapped, { true, false }), PresentMode::Mailbox);
    EXPECT_EQ(ChoosePresentMode(PresentPolicy::Uncapped, { false, true }), PresentMode::Immediate);
    EXPECT_EQ(ChoosePresentMode(PresentPolicy::Uncapped, { false, false }), PresentMode::Fifo);
}

TEST(PresentPolicyTest, NamesAreStable)
{
    EXPECT_STREQ(PresentPolicyName(PresentPolicy::Capped), "capped");
    EXPECT_STREQ(PresentPolicyName(PresentPolicy::Uncapped), "uncapped");
    EXPECT_STREQ(PresentModeName(PresentMode::Fifo), "fifo");
    EXPECT_STREQ(PresentModeName(PresentMode::Mailbox), "mailbox");
    EXPECT_STREQ(PresentModeName(PresentMode::Immediate), "immediate");
}

} // namespace
} // namespace gte
