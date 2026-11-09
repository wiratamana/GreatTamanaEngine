// Tier-1 tests: a host-written ring keeps every copy current and never writes a copy a frame in flight reads.

#include "Renderer/SlotRingBuffer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace gte {
namespace {

// Host memory standing in for one mapped GPU buffer.
class FakeCopy {
public:
    explicit FakeCopy(std::size_t size, int fill = 0)
        : m_bytes(size, static_cast<std::byte>(fill))
    {
    }

    void Upload(const void* data, std::size_t size)
    {
        ASSERT_EQ(size, m_bytes.size());
        std::memcpy(m_bytes.data(), data, size);
        ++uploads;
    }
    void* MappedData() const { return const_cast<std::byte*>(m_bytes.data()); }
    std::uint64_t Size() const { return m_bytes.size(); }
    int First() const { return static_cast<int>(m_bytes[0]); }

    int uploads = 0;

private:
    std::vector<std::byte> m_bytes;
};

using FakeRing = BasicSlotRingBuffer<FakeCopy>;

constexpr std::size_t kSize = 8;

std::shared_ptr<FakeRing> MakeRing(std::uint32_t copies)
{
    std::vector<std::shared_ptr<FakeCopy>> backing;
    for (std::uint32_t i = 0; i < copies; ++i) {
        backing.push_back(std::make_shared<FakeCopy>(kSize));
    }
    return std::make_shared<FakeRing>(std::move(backing));
}

// The frame with this serial in a ring of `copies` slots; `completed` frames have finished.
OffscreenFrameTicket TicketFor(std::uint64_t serial, std::uint32_t copies, std::uint64_t completed)
{
    return OffscreenFrameTicket{ static_cast<std::uint32_t>((serial - 1) % copies), serial, completed };
}

void Write(FakeRing& ring, const OffscreenFrameTicket& ticket, int value, bool expected = true)
{
    const std::vector<std::byte> bytes(kSize, static_cast<std::byte>(value));
    EXPECT_EQ(ring.Write(ticket, bytes.data(), bytes.size()), expected);
}

TEST(SlotRingBufferTest, AWriteLandsInTheCopyOfItsSlotOnly)
{
    const auto ring = MakeRing(2);
    Write(*ring, TicketFor(1, 2, 0), 7);
    EXPECT_EQ(ring->Copy(0).First(), 7);
    EXPECT_EQ(ring->Copy(1).First(), 0);
    EXPECT_EQ(ring->LatestCopy(), 0u);
    EXPECT_EQ(ring->LatestVersion(), 1u);
}

TEST(SlotRingBufferTest, TheSlotIsFoldedIntoTheCopyCount)
{
    const auto ring = MakeRing(2);
    EXPECT_EQ(&ring->Copy(0), &ring->Copy(2));
    EXPECT_EQ(&ring->Copy(1), &ring->Copy(3));
}

TEST(SlotRingBufferTest, AWriteIsRefusedWhileAFrameInFlightReadsTheCopy)
{
    const auto ring = MakeRing(2);
    Write(*ring, TicketFor(1, 2, 0), 1);
    Write(*ring, TicketFor(2, 2, 0), 2);
    // Frame 3 reuses the copy frame 1 wrote; frame 1 is not complete, so the write must be refused untouched.
    Write(*ring, TicketFor(3, 2, 0), 3, /*expected=*/false);
    EXPECT_EQ(ring->Copy(0).First(), 1);
    EXPECT_EQ(ring->LatestVersion(), 2u);
    // Once frame 1 is complete the same write goes through.
    Write(*ring, TicketFor(3, 2, 1), 3);
    EXPECT_EQ(ring->Copy(0).First(), 3);
}

TEST(SlotRingBufferTest, AFrameMayWriteItsOwnCopyAgain)
{
    const auto ring = MakeRing(2);
    Write(*ring, TicketFor(1, 2, 0), 1);
    Write(*ring, TicketFor(1, 2, 0), 2);
    EXPECT_EQ(ring->Copy(0).First(), 2);
}

TEST(SlotRingBufferTest, ACopyBehindTheNewestContentIsCaughtUp)
{
    const auto ring = MakeRing(2);
    Write(*ring, TicketFor(1, 2, 0), 5); // Slot 0 holds the newest content.
    // A frame in slot 1 that writes nothing (a frozen frame) must still draw it.
    EXPECT_TRUE(ring->CatchUp(TicketFor(2, 2, 0)));
    EXPECT_EQ(ring->Copy(1).First(), 5);
    EXPECT_EQ(ring->VersionOf(1), ring->LatestVersion());
}

TEST(SlotRingBufferTest, CatchUpCopiesFromTheNewestCopyNotFromTheOtherOne)
{
    // Three copies: the newest is copy 1, and copy 2 must take it from there, not from copy 0.
    const auto ring = MakeRing(3);
    Write(*ring, TicketFor(1, 3, 0), 10);
    Write(*ring, TicketFor(2, 3, 0), 20);
    EXPECT_EQ(ring->LatestCopy(), 1u);
    EXPECT_TRUE(ring->CatchUp(TicketFor(3, 3, 0)));
    EXPECT_EQ(ring->Copy(2).First(), 20);
}

TEST(SlotRingBufferTest, CatchUpDoesNothingWhenTheCopyIsCurrent)
{
    const auto ring = MakeRing(2);
    Write(*ring, TicketFor(1, 2, 0), 5);
    const int uploads = ring->Copy(0).uploads;
    EXPECT_TRUE(ring->CatchUp(TicketFor(1, 2, 0)));
    EXPECT_EQ(ring->Copy(0).uploads, uploads);
}

TEST(SlotRingBufferTest, CatchUpIsRefusedWhileAFrameInFlightReadsTheStaleCopy)
{
    const auto ring = MakeRing(2);
    Write(*ring, TicketFor(1, 2, 0), 1);
    EXPECT_TRUE(ring->CatchUp(TicketFor(2, 2, 0))); // Copy 1 is now read by frame 2.
    Write(*ring, TicketFor(3, 2, 1), 3);            // Copy 0 gets newer content; frame 1 is done.
    // Copy 1 is behind, but frame 2 may still read it: frame 4 must not rewrite it yet.
    EXPECT_FALSE(ring->CatchUp(TicketFor(4, 2, 1)));
    EXPECT_EQ(ring->Copy(1).First(), 1);
    EXPECT_TRUE(ring->CatchUp(TicketFor(4, 2, 2)));
    EXPECT_EQ(ring->Copy(1).First(), 3);
}

TEST(SlotRingBufferTest, ASynchronousTicketAlwaysWritesCopyZero)
{
    const auto ring = MakeRing(2);
    Write(*ring, OffscreenFrameTicket{}, 9);
    Write(*ring, OffscreenFrameTicket{}, 8);
    EXPECT_EQ(ring->Copy(0).First(), 8);
    EXPECT_EQ(ring->Copy(1).First(), 0);
}

TEST(SlotRingBufferTest, ASingleCopyRingBehavesLikeAPlainBuffer)
{
    const auto ring = MakeRing(1);
    Write(*ring, OffscreenFrameTicket{}, 3);
    EXPECT_EQ(ring->Copies(), 1u);
    EXPECT_EQ(&ring->Copy(0), &ring->Copy(5));
    EXPECT_TRUE(ring->CatchUp(OffscreenFrameTicket{}));
}

} // namespace
} // namespace gte
