// Unit tests for SdlMemoryTracker (src/Memory/SdlMemoryTracker.h) - installs
// a tracking allocator behind SDL_malloc/calloc/realloc/free (see
// SDL_SetMemoryFunctions(), SDL3/SDL_stdinc.h) and exercises it directly by
// calling SDL_malloc()/SDL_free() etc. - none of this needs SDL_Init() or a
// live video subsystem (SDL_malloc and friends are plain allocator
// functions, independent of SDL's subsystems), so this is genuinely Tier 1
// despite touching SDL directly - see
// tests/Application/EventTranslatorTests.cpp for the same "SDL types/
// functions that don't need SDL_Init()" pattern.
//
// Install() is process-global and idempotent (see its own doc comment) -
// every test below captures LiveBytes()/LiveAllocationCount() BEFORE its own
// SDL_malloc/free calls and asserts on the DELTA, rather than assuming a
// zero baseline, so test order/other SDL usage in this same process can
// never make these tests flaky.

#include "Memory/SdlMemoryTracker.h"

#include <SDL3/SDL_stdinc.h>

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SdlMemoryTrackerTest, MallocIncreasesLiveBytesAndCount_FreeUndoesIt)
{
    SdlMemoryTracker::Install();

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = SdlMemoryTracker::LiveAllocationCount();

    void* block = SDL_malloc(256);
    ASSERT_NE(block, nullptr);

    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 256);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore + 1);

    SDL_free(block);

    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(SdlMemoryTrackerTest, CallocTracksTotalNmembTimesSize)
{
    SdlMemoryTracker::Install();

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = SdlMemoryTracker::LiveAllocationCount();

    void* block = SDL_calloc(4, 32); // 128 bytes total.
    ASSERT_NE(block, nullptr);

    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 128);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore + 1);

    // calloc'd memory must actually be zeroed - not just tracked correctly.
    const unsigned char* bytes = static_cast<const unsigned char*>(block);
    for (int i = 0; i < 128; ++i) {
        EXPECT_EQ(bytes[i], 0);
    }

    SDL_free(block);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(SdlMemoryTrackerTest, ReallocGrowingUpdatesLiveBytesByTheDelta)
{
    SdlMemoryTracker::Install();

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = SdlMemoryTracker::LiveAllocationCount();

    void* block = SDL_malloc(64);
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 64);

    void* grown = SDL_realloc(block, 200);
    ASSERT_NE(grown, nullptr);

    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 200);
    // Still one logical allocation - realloc resizes, it doesn't add one.
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore + 1);

    SDL_free(grown);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(SdlMemoryTrackerTest, ReallocFromNullBehavesLikeMalloc)
{
    SdlMemoryTracker::Install();

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();

    void* block = SDL_realloc(nullptr, 48);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 48);

    SDL_free(block);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
}

TEST(SdlMemoryTrackerTest, MultipleOutstandingAllocationsAggregateCorrectly)
{
    SdlMemoryTracker::Install();

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = SdlMemoryTracker::LiveAllocationCount();

    void* a = SDL_malloc(10);
    void* b = SDL_malloc(20);
    void* c = SDL_malloc(30);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 60);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore + 3);

    SDL_free(b); // Free out of order.
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 40);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore + 2);

    SDL_free(a);
    SDL_free(c);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(SdlMemoryTrackerTest, FreeOfNullIsHarmlessNoop)
{
    SdlMemoryTracker::Install();

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();
    const std::uint64_t countBefore = SdlMemoryTracker::LiveAllocationCount();

    SDL_free(nullptr);

    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
    EXPECT_EQ(SdlMemoryTracker::LiveAllocationCount(), countBefore);
}

TEST(SdlMemoryTrackerTest, InstallIsIdempotent)
{
    SdlMemoryTracker::Install();
    SdlMemoryTracker::Install(); // Must not double-wrap/break tracking.

    const std::uint64_t bytesBefore = SdlMemoryTracker::LiveBytes();
    void* block = SDL_malloc(16);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore + 16);
    SDL_free(block);
    EXPECT_EQ(SdlMemoryTracker::LiveBytes(), bytesBefore);
}

} // namespace
} // namespace gte
