// Tests: pool staleness policy and eviction, the strict validation counter, snapshot removal.

#include "Renderer/RenderGraph/OnceErrorLog.h"
#include "Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"
#include "Renderer/RenderGraph/RenderGraphResourcePool.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

namespace gte::rg {
namespace {

using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

TEST(PoolStalenessTest, ExactlyTheLimitIsKeptAndOneMoreIsEvicted)
{
    constexpr std::uint64_t kLast = 10;
    EXPECT_FALSE(IsStalePoolEntry(false, kLast, kLast + kPoolStaleFrames));
    EXPECT_TRUE(IsStalePoolEntry(false, kLast, kLast + kPoolStaleFrames + 1));
}

TEST(PoolStalenessTest, AClaimedEntryIsNeverStale)
{
    EXPECT_FALSE(IsStalePoolEntry(true, 0, 100000));
}

TEST(PoolStalenessTest, AFrameCounterBehindTheClaimIsNotStale)
{
    EXPECT_FALSE(IsStalePoolEntry(false, 500, 3));
}

TEST(PoolStalenessTest, TheLimitIsAboveTheFramesInFlight)
{
    EXPECT_GT(kPoolStaleFrames, std::uint64_t{ 8 });
}

TEST(OnceErrorLogTest, TheStrictCounterCountsOnlyFirstTimeMessages)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog log;

    EXPECT_EQ(log.StrictViolationCount(), 0u);
    log.Error("Cat", "first");
    log.Error("Cat", "first");
    log.Error("Cat", "second");
    log.Error("Other", "first");
    EXPECT_EQ(log.StrictViolationCount(), 3u);
}

TEST(OnceErrorLogTest, StrictValidationFollowsTheBuildOption)
{
    EXPECT_EQ(StrictValidationEnabled(), GTE_STRICT_RENDER_VALIDATION != 0);
}

TEST(DebugTextureRegistryTest, RemoveIfDropsOnlyTheMatchingSnapshots)
{
    RenderGraphDebugTextureRegistry registry;
    for (const char* name : { "A", "B", "C" }) {
        DebugTextureSnapshot snapshot;
        snapshot.name = name;
        snapshot.target.image = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(name[0]));
        registry.Upsert(snapshot);
    }

    const VkImage dropped = reinterpret_cast<VkImage>(static_cast<std::uintptr_t>('B'));
    registry.RemoveIf([dropped](const DebugTextureSnapshot& s) { return s.target.image == dropped; });

    EXPECT_FALSE(registry.FindByName("B").has_value());
    EXPECT_TRUE(registry.FindByName("A").has_value());
    EXPECT_EQ(registry.ListAll().size(), 2u);
}

// Tier-2: a texture and a buffer idle past the limit are freed in BeginFrame; a claimed one survives.
TEST(PoolEvictionGpuTest, IdleEntriesAreFreedAndClaimedOnesAreKept)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RenderGraphResourcePool pool(fixture.GetRenderer());

    TextureDesc desc;
    desc.width = 32;
    desc.height = 32;
    desc.format = PixelFormat::RGBA8_UNorm;
    BufferDesc bufferDesc;
    bufferDesc.size = 256;
    bufferDesc.usage = BufferUsage::StorageBuffer;

    pool.BeginFrame(1);
    pool.AcquireTexture(desc, "PoolTexture");
    pool.AcquireBuffer(bufferDesc, "PoolBuffer");
    EXPECT_EQ(pool.TextureEntryCount(), 1u);
    EXPECT_EQ(pool.BufferEntryCount(), 1u);

    pool.BeginFrame(1 + kPoolStaleFrames);
    EXPECT_EQ(pool.TextureEntryCount(), 1u);
    EXPECT_EQ(pool.EvictedEntryCount(), 0u);

    pool.BeginFrame(2 + kPoolStaleFrames);
    EXPECT_EQ(pool.TextureEntryCount(), 0u);
    EXPECT_EQ(pool.BufferEntryCount(), 0u);
    EXPECT_EQ(pool.EvictedEntryCount(), 2u);
    EXPECT_EQ(pool.TakeEvictedImages().size(), 1u);
}

TEST(PoolEvictionGpuTest, AReclaimedEntryIsReusedNotRecreated)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    RenderGraphResourcePool pool(fixture.GetRenderer());
    TextureDesc desc;
    desc.width = 16;
    desc.height = 16;
    desc.format = PixelFormat::RGBA8_UNorm;

    pool.BeginFrame(1);
    Texture& first = pool.AcquireTexture(desc, "PoolTexture");
    for (std::uint64_t frame = 2; frame < 2 + 3 * kPoolStaleFrames; ++frame) {
        pool.BeginFrame(frame);
        EXPECT_EQ(&pool.AcquireTexture(desc, "PoolTexture"), &first);
    }
    EXPECT_EQ(pool.EvictedEntryCount(), 0u);
}

} // namespace
} // namespace gte::rg
