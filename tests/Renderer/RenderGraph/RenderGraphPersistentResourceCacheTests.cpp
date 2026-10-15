// Tier-2 (headless GPU) tests for RenderGraphPersistentResourceCache.
// Skipped when the headless fixture is unusable.

#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gte::rg {
namespace {

constexpr const char* kTestOwner = "PersistentResourceCacheValidation";

// A fresh, color-only TextureDesc.
TextureDesc MakeColorDesc(std::uint32_t width = 64, std::uint32_t height = 64)
{
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = PixelFormat::RGBA8_UNorm;
    desc.hasDepth = false;
    return desc;
}

constexpr ExecuteTimingMode kSync = ExecuteTimingMode::SynchronousImmediateReadback;
constexpr ExecuteTimingMode kPipelined = ExecuteTimingMode::PipelinedDeferredReadback;

using test::CountByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

constexpr std::string_view kCacheLogCategory = "RenderGraphPersistentResourceCache";

} // namespace

// Resolve() across three separate frames returns the same texture and image.
TEST(RenderGraphPersistentResourceCacheTest, ResolveReturnsTheSamePhysicalTextureAcrossThreeSeparateFrames)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    RenderTexture* firstTexture = nullptr;
    VkImage firstImage = VK_NULL_HANDLE;

    for (int frame = 0; frame < 3; ++frame) {
        std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
        fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
            resolved = cache.Resolve(kTestOwner, "BasicReuse", desc, static_cast<std::uint64_t>(frame + 1), kSync);
            return {};
        });

        ASSERT_TRUE(resolved.has_value());
        ASSERT_NE(resolved->texture, nullptr);
        if (frame == 0) {
            firstTexture = resolved->texture;
            firstImage = resolved->texture->Image();
            EXPECT_NE(firstImage, static_cast<VkImage>(VK_NULL_HANDLE));
        } else {
            EXPECT_EQ(resolved->texture, firstTexture);
            EXPECT_EQ(resolved->texture->Image(), firstImage);
        }
    }
}

// The very first request may happen at frame 0; only a second request in the same frame is refused.
TEST(RenderGraphPersistentResourceCacheTest, FirstRequestAtFrameZeroIsAcceptedAndASecondOneInThatFrameIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RecordingLogSink sink;
    const ScopedLogSinkInstall logGuard(&sink);
    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> first;
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> second;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        first = cache.Resolve(kTestOwner, "FrameZero", desc, /*currentFrame=*/0, kSync);
        second = cache.Resolve(kTestOwner, "FrameZero", desc, /*currentFrame=*/0, kSync);
        return {};
    });

    EXPECT_TRUE(first.has_value());
    EXPECT_FALSE(second.has_value());
    EXPECT_EQ(CountByCategory(sink, kCacheLogCategory), 1);
}

// Two owners with the same name and desc never share a texture.
TEST(RenderGraphPersistentResourceCacheTest, TwoOwnersWithTheSameNameAndDescNeverCollide)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolvedA;
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolvedB;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolvedA = cache.Resolve("OwnerA", "History", desc, /*currentFrame=*/1, kSync);
        resolvedB = cache.Resolve("OwnerB", "History", desc, /*currentFrame=*/1, kSync);
        return {};
    });

    ASSERT_TRUE(resolvedA.has_value());
    ASSERT_TRUE(resolvedB.has_value());
    ASSERT_NE(resolvedA->texture, nullptr);
    ASSERT_NE(resolvedB->texture, nullptr);
    EXPECT_NE(resolvedA->texture, resolvedB->texture);
    EXPECT_NE(resolvedA->texture->Image(), resolvedB->texture->Image());
}

// A failed create throws and leaves the key retryable.
TEST(RenderGraphPersistentResourceCacheTest, FailedResolveThrowsAndLeavesTheKeyRetryableAfterward)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const std::uint32_t impossibleExtent = fixture.UnsupportedImageExtent();
    const TextureDesc hugeDesc = MakeColorDesc(impossibleExtent, impossibleExtent);

    bool threw = false;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        try {
            (void)cache.Resolve(kTestOwner, "ExceptionSafety", hugeDesc, /*currentFrame=*/1, kSync);
        } catch (const std::exception&) {
            threw = true;
        }
        return {};
    });
    ASSERT_TRUE(threw) << "an image larger than maxImageDimension2D must be rejected";

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> retryResolved;
    const TextureDesc saneDesc = MakeColorDesc();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        retryResolved = cache.Resolve(kTestOwner, "ExceptionSafety", saneDesc, /*currentFrame=*/2, kSync);
        return {};
    });

    ASSERT_TRUE(retryResolved.has_value());
    EXPECT_NE(retryResolved->texture, nullptr);
}

// Debug builds only: invalid arguments hit an assert before the log-and-refuse fallback.
#ifndef NDEBUG

TEST(RenderGraphPersistentResourceCacheDeathTest, ResolveAssertsWhenDescHasDepthIsTrue)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
            TextureDesc desc = MakeColorDesc();
            desc.hasDepth = true;
            (void)cache.Resolve(kTestOwner, "DepthRefusal", desc, /*currentFrame=*/1, kSync);
        },
        "");
}

TEST(RenderGraphPersistentResourceCacheDeathTest, ResolveAssertsOnNullOwner)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
            const TextureDesc desc = MakeColorDesc();
            (void)cache.Resolve(nullptr, "NullOwnerRefusal", desc, /*currentFrame=*/1, kSync);
        },
        "");
}

TEST(RenderGraphPersistentResourceCacheDeathTest, ResolveAssertsOnNullName)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
            const TextureDesc desc = MakeColorDesc();
            (void)cache.Resolve(kTestOwner, nullptr, desc, /*currentFrame=*/1, kSync);
        },
        "");
}

#endif // !NDEBUG

// Empty owner is a deliberate escape hatch: the identity stays the literal `name`, no "<owner>::" prefix.
TEST(RenderGraphPersistentResourceCacheTest, ResolveWithEmptyOwnerUsesNameAsLiteralIdentity)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolved = cache.Resolve("", "LiteralIdentity", desc, /*currentFrame=*/1, kSync);
        return {};
    });

    ASSERT_TRUE(resolved.has_value());
    ASSERT_NE(resolved->combinedKey, nullptr);
    EXPECT_EQ(*resolved->combinedKey, "LiteralIdentity");
}

#ifndef NDEBUG

TEST(RenderGraphPersistentResourceCacheDeathTest, ResolveAssertsOnEmptyName)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
            const TextureDesc desc = MakeColorDesc();
            (void)cache.Resolve(kTestOwner, "", desc, /*currentFrame=*/1, kSync);
        },
        "");
}

#endif // !NDEBUG

// Render textures and depth buffers both register as Texture: a delta of exactly 1 proves no depth companion.
TEST(RenderGraphPersistentResourceCacheTest, ResolveNeverAllocatesADepthCompanion)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    const GpuMemoryTracker::Totals before = fixture.GetRenderer().GetMemoryTotals();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolved = cache.Resolve(kTestOwner, "NoDepthCheck", desc, /*currentFrame=*/1, kSync);
        return {};
    });

    ASSERT_TRUE(resolved.has_value());

    const GpuMemoryTracker::Totals after = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(after.textureCount, before.textureCount + 1);
}

// Map growth must not move earlier entries: their pointers and images stay valid.
TEST(RenderGraphPersistentResourceCacheTest, EarlierResolvedIdentitiesStayValidAfterManyMoreAreAdded)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());

    struct Recorded {
        std::string name;
        RenderTexture* texture = nullptr;
        VkImage image = VK_NULL_HANDLE;
        Extent2D extent{};
    };

    std::vector<Recorded> recorded;
    constexpr int kIdentityCount = 12;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        for (int i = 0; i < kIdentityCount; ++i) {
            const std::string name = "Identity" + std::to_string(i);
            const TextureDesc desc =
                MakeColorDesc(32u + static_cast<std::uint32_t>(i), 32u + static_cast<std::uint32_t>(i));
            std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
                cache.Resolve(kTestOwner, name.c_str(), desc, /*currentFrame=*/1, kSync);
            if (!resolved.has_value() || resolved->texture == nullptr) {
                ADD_FAILURE() << "Resolve() unexpectedly failed for identity " << name;
                continue;
            }
            recorded.push_back(
                Recorded{ name, resolved->texture, resolved->texture->Image(), resolved->texture->Extent() });
        }
        return {};
    });

    ASSERT_EQ(recorded.size(), static_cast<std::size_t>(kIdentityCount));

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        for (const Recorded& entry : recorded) {
            const TextureDesc desc = MakeColorDesc(entry.extent.width, entry.extent.height);
            std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
                cache.Resolve(kTestOwner, entry.name.c_str(), desc, /*currentFrame=*/2, kSync);
            if (!resolved.has_value()) {
                ADD_FAILURE() << "Re-Resolve() unexpectedly failed for identity " << entry.name;
                continue;
            }
            EXPECT_EQ(resolved->texture, entry.texture);
            EXPECT_EQ(resolved->texture->Image(), entry.image);
            EXPECT_EQ(resolved->texture->Extent().width, entry.extent.width);
            EXPECT_EQ(resolved->texture->Extent().height, entry.extent.height);
        }
        return {};
    });
}

// An entry idle for more than the threshold is destroyed on the next sweep. A small threshold avoids 300 frames.
TEST(RenderGraphPersistentResourceCacheTest, EvictionRemovesAnIdleEntryAndFreesItsGpuMemory)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();
    constexpr std::uint64_t kSmallThreshold = 2;

    const GpuMemoryTracker::Totals before = fixture.GetRenderer().GetMemoryTotals();

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
            cache.Resolve(kTestOwner, "EvictMe", desc, /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Resolve() unexpectedly failed for EvictMe";
        }
        return {};
    });
    cache.BeginFrame(1, kSmallThreshold); // Elapsed 0: never evicted on its creation frame.

    const GpuMemoryTracker::Totals afterCreate = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(afterCreate.textureCount, before.textureCount + 1);
    EXPECT_TRUE(cache.FramesUntilEviction(kTestOwner, "EvictMe", 1).has_value());

    cache.BeginFrame(2, kSmallThreshold); // Elapsed 1, threshold 2: still alive.
    EXPECT_TRUE(cache.FramesUntilEviction(kTestOwner, "EvictMe", 2).has_value());

    cache.BeginFrame(4, kSmallThreshold); // Elapsed 3 > 2: evicted.
    EXPECT_FALSE(cache.FramesUntilEviction(kTestOwner, "EvictMe", 4).has_value());

    const GpuMemoryTracker::Totals afterEvict = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(afterEvict.textureCount, before.textureCount);
}

// An entry requested every frame is never evicted mid-use.
TEST(RenderGraphPersistentResourceCacheTest, EntryRequestedEveryFrameIsNeverEvicted)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();
    constexpr std::uint64_t kSmallThreshold = 2;

    RenderTexture* firstTexture = nullptr;
    for (std::uint64_t frame = 1; frame <= 10; ++frame) {
        fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
            std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
                cache.Resolve(kTestOwner, "AlwaysActive", desc, frame, kSync);
            if (!resolved.has_value() || resolved->texture == nullptr) {
                ADD_FAILURE() << "Resolve() unexpectedly failed on frame " << frame;
                return {};
            }
            if (frame == 1) {
                firstTexture = resolved->texture;
            } else {
                EXPECT_EQ(resolved->texture, firstTexture);
            }
            return {};
        });
        cache.BeginFrame(frame, kSmallThreshold);
    }

    EXPECT_TRUE(cache.FramesUntilEviction(kTestOwner, "AlwaysActive", 10).has_value());
}

// FramesUntilEviction()'s two overloads agree with each other.
TEST(RenderGraphPersistentResourceCacheTest, FramesUntilEvictionOverloadsAgree)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolved = cache.Resolve(kTestOwner, "AgreeCheck", desc, /*currentFrame=*/1, kSync);
        return {};
    });
    ASSERT_TRUE(resolved.has_value());
    ASSERT_NE(resolved->combinedKey, nullptr);

    const std::optional<std::uint64_t> byCombined = cache.FramesUntilEviction(*resolved->combinedKey, 5);
    const std::optional<std::uint64_t> byOwnerName = cache.FramesUntilEviction(kTestOwner, "AgreeCheck", 5);
    ASSERT_TRUE(byCombined.has_value());
    ASSERT_TRUE(byOwnerName.has_value());
    EXPECT_EQ(*byCombined, *byOwnerName);
}

// A second request for one identity within the same frame is refused and logged once, never per call.
TEST(RenderGraphPersistentResourceCacheTest, DoubleRequestWithinTheSameFrameIsRefusedAndLoggedExactlyOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> first;
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> second;
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> third;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        first = cache.Resolve(kTestOwner, "Dup", desc, /*currentFrame=*/5, kSync);
        second = cache.Resolve(kTestOwner, "Dup", desc, /*currentFrame=*/5, kSync);
        third = cache.Resolve(kTestOwner, "Dup", desc, /*currentFrame=*/5, kSync);
        return {};
    });

    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(second.has_value());
    EXPECT_FALSE(third.has_value());

    int matchingErrorCount = 0;
    for (const RecordingLogSink::Recorded& entry : sink.entries) {
        if (entry.category == kCacheLogCategory && entry.level == LogLevel::Error
            && entry.message.find("Dup") != std::string::npos) {
            ++matchingErrorCount;
        }
    }
    EXPECT_EQ(matchingErrorCount, 1);
}

// A same-frame refusal does not poison the entry: a later frame gets the same texture.
TEST(RenderGraphPersistentResourceCacheTest, ARequestInALaterFrameAfterASameFrameRefusalSucceedsNormally)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> frame5First;
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> frame5Second;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        frame5First = cache.Resolve(kTestOwner, "LaterFrameRetry", desc, /*currentFrame=*/5, kSync);
        frame5Second = cache.Resolve(kTestOwner, "LaterFrameRetry", desc, /*currentFrame=*/5, kSync);
        return {};
    });
    ASSERT_TRUE(frame5First.has_value());
    EXPECT_FALSE(frame5Second.has_value());

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> frame6;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        frame6 = cache.Resolve(kTestOwner, "LaterFrameRetry", desc, /*currentFrame=*/6, kSync);
        return {};
    });
    ASSERT_TRUE(frame6.has_value());
    EXPECT_EQ(frame6->texture, frame5First->texture);
}

// A token that outlives its entry's eviction reports dead and never touches freed memory,
// even after the slot is reused by a new identity.
TEST(RenderGraphPersistentResourceCacheTest, IsTokenLiveReturnsFalseForAnEvictedEntryWithoutCrashing)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();
    constexpr std::uint64_t kSmallThreshold = 1;

    PersistentTextureCacheToken staleToken;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
            cache.Resolve(kTestOwner, "StaleToken", desc, /*currentFrame=*/1, kSync);
        if (resolved.has_value()) {
            staleToken.slot = resolved->slot;
        }
        return {};
    });

    ASSERT_NE(staleToken.slot.epoch, 0u);
    EXPECT_TRUE(cache.IsTokenLive(staleToken));

    cache.BeginFrame(1, kSmallThreshold);
    cache.BeginFrame(10, kSmallThreshold);
    ASSERT_FALSE(cache.FramesUntilEviction(kTestOwner, "StaleToken", 10).has_value());
    EXPECT_FALSE(cache.IsTokenLive(staleToken));

    // A new identity reuses the freed slot index, but the old token stays dead.
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> reused;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        reused = cache.Resolve(kTestOwner, "AfterEviction", desc, /*currentFrame=*/10, kSync);
        return {};
    });
    ASSERT_TRUE(reused.has_value());
    EXPECT_EQ(reused->slot.index, staleToken.slot.index);
    EXPECT_NE(reused->slot.epoch, staleToken.slot.epoch);
    EXPECT_FALSE(cache.IsTokenLive(staleToken));
    EXPECT_FALSE(cache.ResolveFast(staleToken.slot, desc, 11, kSync).has_value());
}

// A resize queued for an entry that is evicted before the flush is skipped.
TEST(RenderGraphPersistentResourceCacheTest, FlushSkipsResizeOfAnEvictedEntry)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kTestOwner, "EvictedResize", MakeColorDesc(64, 64), 1, kSync);
        (void)cache.Resolve(kTestOwner, "EvictedResize", MakeColorDesc(128, 128), 2, kSync);
        return {};
    });
    cache.BeginFrame(100, /*staleThresholdFrames=*/1);
    EXPECT_TRUE(cache.FlushPendingResizes().empty()); // Must not touch the evicted entry.
    EXPECT_FALSE(cache.FramesUntilEviction(kTestOwner, "EvictedResize", 100).has_value());
}

// A resize retires the old texture instead of destroying it; BeginFrame frees it.
TEST(RenderGraphPersistentResourceCacheTest, FlushRetiresTheOldTextureUntilTheNextBeginFrame)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kTestOwner, "Retire", MakeColorDesc(64, 64), 1, kSync);
        (void)cache.Resolve(kTestOwner, "Retire", MakeColorDesc(128, 128), 2, kSync);
        return {};
    });

    const GpuMemoryTracker::Totals before = fixture.GetRenderer().GetMemoryTotals();
    ASSERT_EQ(cache.FlushPendingResizes().size(), 1u);

    // Old and new image both exist until the next frame starts.
    EXPECT_EQ(cache.RetiredTextureCount(), 1u);
    EXPECT_EQ(fixture.GetRenderer().GetMemoryTotals().textureCount, before.textureCount + 1);

    cache.BeginFrame(3, kPersistentResourceStaleThresholdFrames);
    EXPECT_EQ(cache.RetiredTextureCount(), 0u);
    EXPECT_EQ(fixture.GetRenderer().GetMemoryTotals().textureCount, before.textureCount);
}

// A resize whose create fails keeps the old texture and is not retried at the same size.
TEST(RenderGraphPersistentResourceCacheTest, FailedResizeKeepsTheOldTexture)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const std::uint32_t impossibleExtent = fixture.UnsupportedImageExtent();
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> first;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        first = cache.Resolve(kTestOwner, "FailResize", MakeColorDesc(64, 64), 1, kSync);
        (void)cache.Resolve(kTestOwner, "FailResize", MakeColorDesc(impossibleExtent, impossibleExtent), 2, kSync);
        return {};
    });
    ASSERT_TRUE(first.has_value());
    const VkImage oldImage = first->texture->Image();

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    const std::vector<PersistentSlotRef> resized = cache.FlushPendingResizes();
    ASSERT_TRUE(resized.empty()) << "an image larger than maxImageDimension2D must be rejected";

    EXPECT_EQ(first->texture->Image(), oldImage);
    EXPECT_EQ(first->texture->Extent().width, 64u);
    EXPECT_EQ(cache.RetiredTextureCount(), 0u);
    EXPECT_EQ(cache.TextureFor(first->slot), first->texture);
    EXPECT_EQ(CountByCategory(sink, kCacheLogCategory), 1); // Exactly one error.

    // The same failing size is not queued again.
    (void)cache.Resolve(kTestOwner, "FailResize", MakeColorDesc(impossibleExtent, impossibleExtent), 3, kSync);
    EXPECT_TRUE(cache.FlushPendingResizes().empty());
    EXPECT_EQ(CountByCategory(sink, kCacheLogCategory), 1);
}

// Debug builds only: the guard does not exist in release builds.
#ifndef NDEBUG
TEST(RenderGraphPersistentResourceCacheTest, DebugTokenIdentityMatchesConfirmsOrRefutesTheOriginalOwnerName)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolved = cache.Resolve(kTestOwner, "IdentityCheck", desc, /*currentFrame=*/1, kSync);
        return {};
    });
    ASSERT_TRUE(resolved.has_value());

    EXPECT_TRUE(cache.DebugTokenIdentityMatches(resolved->slot, kTestOwner, "IdentityCheck"));
    EXPECT_FALSE(cache.DebugTokenIdentityMatches(resolved->slot, "SomeoneElse", "IdentityCheck"));
    EXPECT_TRUE(cache.DebugTokenIdentityMatches(PersistentSlotRef{}, "Anything", "Anything"));
}
#endif // !NDEBUG

// A re-requested identity with a new size is queued, not resized inline. Only FlushPendingResizes() applies it
// to the same RenderTexture and resets the layout to UNDEFINED.
TEST(RenderGraphPersistentResourceCacheTest, ResizeIsQueuedNotAppliedUntilFlushPendingResizesIsCalled)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc64 = MakeColorDesc(64, 64);
    const TextureDesc desc128 = MakeColorDesc(128, 128);

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> firstResolve;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        firstResolve = cache.Resolve(kTestOwner, "Resize", desc64, /*currentFrame=*/1, kSync);
        return {};
    });
    ASSERT_TRUE(firstResolve.has_value());
    RenderTexture* texture = firstResolve->texture;
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->Extent().width, 64u);

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> secondResolve;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        secondResolve = cache.Resolve(kTestOwner, "Resize", desc128, /*currentFrame=*/2, kSync);
        return {};
    });
    ASSERT_TRUE(secondResolve.has_value());
    // Not resized yet: this call still sees the old extent.
    EXPECT_EQ(secondResolve->texture, texture);
    EXPECT_EQ(secondResolve->texture->Extent().width, 64u);

    const std::vector<PersistentSlotRef> resized = cache.FlushPendingResizes();
    ASSERT_EQ(resized.size(), 1u);
    EXPECT_EQ(resized[0], secondResolve->slot);
    EXPECT_EQ(cache.TextureFor(resized[0]), texture);
    EXPECT_TRUE(cache.FlushPendingResizes().empty()); // Nothing pending anymore.

    // After the flush the same RenderTexture has the new size.
    EXPECT_EQ(texture->Extent().width, 128u);
    EXPECT_EQ(texture->Extent().height, 128u);

    // The recreated image is UNDEFINED again.
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> thirdResolve;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        thirdResolve = cache.Resolve(kTestOwner, "Resize", desc128, /*currentFrame=*/3, kSync);
        return {};
    });
    ASSERT_TRUE(thirdResolve.has_value());
    EXPECT_EQ(thirdResolve->lastKnownLayout, VK_IMAGE_LAYOUT_UNDEFINED);
}

// Three entries queue different new sizes across two frames; one flush applies all of them.
TEST(RenderGraphPersistentResourceCacheTest, BatchedResizeAppliesAllQueuedEntriesInOneFlushCall)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());

    RenderTexture* textureA = nullptr;
    RenderTexture* textureB = nullptr;
    RenderTexture* textureC = nullptr;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto a = cache.Resolve(kTestOwner, "BatchA", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        auto b = cache.Resolve(kTestOwner, "BatchB", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        auto c = cache.Resolve(kTestOwner, "BatchC", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!a.has_value() || !b.has_value() || !c.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for one of BatchA/BatchB/BatchC";
            return {};
        }
        textureA = a->texture;
        textureB = b->texture;
        textureC = c->texture;
        return {};
    });
    ASSERT_NE(textureA, nullptr);
    ASSERT_NE(textureB, nullptr);
    ASSERT_NE(textureC, nullptr);

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kTestOwner, "BatchA", MakeColorDesc(64, 64), /*currentFrame=*/2, kSync);
        (void)cache.Resolve(kTestOwner, "BatchB", MakeColorDesc(96, 96), /*currentFrame=*/2, kSync);
        (void)cache.Resolve(kTestOwner, "BatchC", MakeColorDesc(128, 128), /*currentFrame=*/2, kSync);
        return {};
    });

    // Nothing is resized before the flush.
    EXPECT_EQ(textureA->Extent().width, 32u);
    EXPECT_EQ(textureB->Extent().width, 32u);
    EXPECT_EQ(textureC->Extent().width, 32u);

    cache.FlushPendingResizes();

    EXPECT_EQ(textureA->Extent().width, 64u);
    EXPECT_EQ(textureB->Extent().width, 96u);
    EXPECT_EQ(textureC->Extent().width, 128u);
}

// Two resize requests for one identity on separate frames: only the later one applies.
// (Same-frame repeats are refused earlier, so they never reach the resize queue.)
TEST(RenderGraphPersistentResourceCacheTest, LastResizeRequestBeforeAFlushWinsOverAnEarlierOne)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    RenderTexture* texture = nullptr;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved = cache.Resolve(kTestOwner, "LastWins", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for LastWins";
            return {};
        }
        texture = resolved->texture;
        return {};
    });
    ASSERT_NE(texture, nullptr);

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kTestOwner, "LastWins", MakeColorDesc(64, 64), /*currentFrame=*/2, kSync);
        return {};
    });
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kTestOwner, "LastWins", MakeColorDesc(96, 96), /*currentFrame=*/3, kSync);
        return {};
    });

    EXPECT_EQ(texture->Extent().width, 32u); // Nothing flushed yet.

    cache.FlushPendingResizes();

    EXPECT_EQ(texture->Extent().width, 96u);
    EXPECT_EQ(texture->Extent().height, 96u);
}

// A resize requested in the pipelined regime is refused, but the call still succeeds and queues nothing.
TEST(RenderGraphPersistentResourceCacheTest, ResizeRequestedFromThePipelinedRegimeIsRefusedButTheCallStillSucceeds)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    RenderTexture* texture = nullptr;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved = cache.Resolve(kTestOwner, "PipelinedRefusal", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for PipelinedRefusal";
            return {};
        }
        texture = resolved->texture;
        return {};
    });
    ASSERT_NE(texture, nullptr);

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> pipelinedResolve;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        pipelinedResolve =
            cache.Resolve(kTestOwner, "PipelinedRefusal", MakeColorDesc(128, 128), /*currentFrame=*/2, kPipelined);
        return {};
    });

    ASSERT_TRUE(pipelinedResolve.has_value());
    EXPECT_EQ(pipelinedResolve->texture, texture);
    EXPECT_EQ(texture->Extent().width, 32u);

    cache.FlushPendingResizes(); // Nothing was queued.
    EXPECT_EQ(texture->Extent().width, 32u);
}

// A format change on an existing entry is a hard refusal in every regime, never a resize.
TEST(RenderGraphPersistentResourceCacheTest, ReRequestingAnExistingEntryWithADifferentFormatIsRefusedRegardlessOfRegime)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved = cache.Resolve(kTestOwner, "FormatRefusal", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for FormatRefusal";
        }
        return {};
    });

    TextureDesc differentFormatDesc = MakeColorDesc(32, 32);
    differentFormatDesc.format = PixelFormat::RGBA16_Float;

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> refusedSync;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        refusedSync = cache.Resolve(kTestOwner, "FormatRefusal", differentFormatDesc, /*currentFrame=*/2, kSync);
        return {};
    });
    EXPECT_FALSE(refusedSync.has_value());

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> refusedPipelined;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        refusedPipelined = cache.Resolve(kTestOwner, "FormatRefusal", differentFormatDesc, /*currentFrame=*/3, kPipelined);
        return {};
    });
    EXPECT_FALSE(refusedPipelined.has_value());
}

// The frame counter starts at 0 and advances once per BeginPersistentResourceFrame().
TEST(RenderGraphPersistentResourceCacheTest, CurrentPersistentResourceFrameCounterAdvancesOncePerBeginCall)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraph& renderGraph = fixture.GetRenderGraph();
    EXPECT_EQ(renderGraph.CurrentPersistentResourceFrameCounter(), 0u);

    renderGraph.BeginPersistentResourceFrame();
    EXPECT_EQ(renderGraph.CurrentPersistentResourceFrameCounter(), 1u);

    renderGraph.BeginPersistentResourceFrame();
    EXPECT_EQ(renderGraph.CurrentPersistentResourceFrameCounter(), 2u);

    renderGraph.BeginPersistentResourceFrame();
    EXPECT_EQ(renderGraph.CurrentPersistentResourceFrameCounter(), 3u);
}

// Builder tests: call BeginPersistentResourceFrame() each frame and touch the handle in a pass
// (culled passes never resolve a texture, so no debug snapshot exists).

// The same VkImage is returned across three frames when one token is reused.
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureReturnsTheSamePhysicalTextureAcrossThreeFrames)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    const std::string combinedKey = std::string(kTestOwner) + "::BuilderReuse";
    PersistentTextureCacheToken token;
    VkImage firstImage = VK_NULL_HANDLE;

    for (int frame = 0; frame < 3; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h = b.GetOrCreatePersistentTexture(token, kTestOwner, "BuilderReuse", desc);
            EXPECT_TRUE(h.IsValid());
            b.AddPass(
                "BuilderReuseTouchPass",
                [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
                [](PassContext&) {});
            return {};
        });

        const std::optional<DebugTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
        ASSERT_TRUE(snapshot.has_value());
        if (frame == 0) {
            firstImage = snapshot->target.image;
            EXPECT_NE(firstImage, static_cast<VkImage>(VK_NULL_HANDLE));
        } else {
            EXPECT_EQ(snapshot->target.image, firstImage);
        }
    }
}

// The first call mints a live token slot; later calls keep that slot and the same image.
// The graph's cache is private, so slot stability stands in for IsTokenLive().
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureTokenFastPathReusesTheSameImage)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    const std::string combinedKey = std::string(kTestOwner) + "::TokenReuse";
    PersistentTextureCacheToken token;
    PersistentSlotRef firstSlot;
    VkImage firstImage = VK_NULL_HANDLE;

    for (int frame = 0; frame < 3; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h = b.GetOrCreatePersistentTexture(token, kTestOwner, "TokenReuse", desc);
            EXPECT_TRUE(h.IsValid());
            b.AddPass(
                "TokenReuseTouchPass",
                [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
                [](PassContext&) {});
            return {};
        });

        const std::optional<DebugTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
        ASSERT_TRUE(snapshot.has_value());
        if (frame == 0) {
            ASSERT_NE(token.slot.epoch, 0u);
            firstSlot = token.slot;
            firstImage = snapshot->target.image;
            EXPECT_NE(firstImage, static_cast<VkImage>(VK_NULL_HANDLE));
        } else {
            EXPECT_EQ(token.slot, firstSlot);
            EXPECT_EQ(snapshot->target.image, firstImage);
        }
    }
}

// Content written on frame N is still there on frame N+1: the layout bookkeeping must not discard it.
// Frame 2 uses a write pass without a clear color (LOAD, not CLEAR); a read-only pass would be culled.
TEST(RenderGraphPersistentResourceCacheTest, ContentWrittenOnFrameNSurvivesReadableOnFrameNPlusOne)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc(16, 16);
    const std::array<float, 4> knownColor{ 0.25f, 0.5f, 0.75f, 1.0f };
    const std::string combinedKey = std::string(kTestOwner) + "::WriteReadRoundTrip";
    PersistentTextureCacheToken token;

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle h = b.GetOrCreatePersistentTexture(token, kTestOwner, "WriteReadRoundTrip", desc);
        b.AddPass(
            "PersistentTextureKnownPatternWrite",
            [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h, knownColor); },
            [](PassContext&) {});
        return {};
    });

    const std::optional<DebugTextureSnapshot> afterFrame1 = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
    ASSERT_TRUE(afterFrame1.has_value());
    const Renderer::CapturedRawPixels pixelsAfterFrame1 = fixture.GetRenderer().CaptureImagePixels(
        afterFrame1->target.image, VK_IMAGE_ASPECT_COLOR_BIT, afterFrame1->target.format,
        FromVkExtent2D(afterFrame1->target.extent), afterFrame1->colorState);
    ASSERT_EQ(pixelsAfterFrame1.pixels.size(), static_cast<std::size_t>(16 * 16 * 4));
    // VK_FORMAT_R8G8B8A8_UNORM: each channel is round(component * 255).
    EXPECT_NEAR(pixelsAfterFrame1.pixels[0], 64, 2);
    EXPECT_NEAR(pixelsAfterFrame1.pixels[1], 128, 2);
    EXPECT_NEAR(pixelsAfterFrame1.pixels[2], 191, 2);
    EXPECT_NEAR(pixelsAfterFrame1.pixels[3], 255, 2);

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle h = b.GetOrCreatePersistentTexture(token, kTestOwner, "WriteReadRoundTrip", desc);
        b.AddPass(
            "PersistentTextureKnownPatternLoadPreserve",
            [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); }, // No clear color: LOAD.
            [](PassContext&) {});
        return {};
    });

    const std::optional<DebugTextureSnapshot> afterFrame2 = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
    ASSERT_TRUE(afterFrame2.has_value());
    EXPECT_EQ(afterFrame2->target.image, afterFrame1->target.image); // Same VkImage: no discard or recreate.
    const Renderer::CapturedRawPixels pixelsAfterFrame2 = fixture.GetRenderer().CaptureImagePixels(
        afterFrame2->target.image, VK_IMAGE_ASPECT_COLOR_BIT, afterFrame2->target.format,
        FromVkExtent2D(afterFrame2->target.extent), afterFrame2->colorState);
    ASSERT_EQ(pixelsAfterFrame2.pixels.size(), pixelsAfterFrame1.pixels.size());
    EXPECT_NEAR(pixelsAfterFrame2.pixels[0], 64, 2);
    EXPECT_NEAR(pixelsAfterFrame2.pixels[1], 128, 2);
    EXPECT_NEAR(pixelsAfterFrame2.pixels[2], 191, 2);
    EXPECT_NEAR(pixelsAfterFrame2.pixels[3], 255, 2);
}

// A write-only pass on a persistent handle with no reader is never culled: its snapshot advances every frame.
TEST(RenderGraphPersistentResourceCacheTest, WriteOnlyPersistentTextureWithNoReaderSurvivesCullingEveryFrame)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    const std::string combinedKey = std::string(kTestOwner) + "::KeepAliveOnly";
    PersistentTextureCacheToken token;

    for (int frame = 0; frame < 3; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        const std::uint64_t counterBefore = fixture.GetRenderGraph().CurrentDebugTextureFrameCounter();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h = b.GetOrCreatePersistentTexture(token, kTestOwner, "KeepAliveOnly", desc);
            b.AddPass(
                "PersistentKeepAliveWritePass",
                [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
                [](PassContext&) {});
            return {}; // `h` is never a final output.
        });

        const std::optional<DebugTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
        ASSERT_TRUE(snapshot.has_value());
        EXPECT_GT(snapshot->lastUpdatedFrameCounter, counterBefore);
    }
}

// Two owners with the same name and desc get separate textures through the builder.
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureTwoOwnersWithTheSameNameNeverCollide)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    PersistentTextureCacheToken tokenA;
    PersistentTextureCacheToken tokenB;

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle a = b.GetOrCreatePersistentTexture(tokenA, "BuilderOwnerA", "History", desc);
        const TextureHandle c = b.GetOrCreatePersistentTexture(tokenB, "BuilderOwnerB", "History", desc);
        EXPECT_TRUE(a.IsValid());
        EXPECT_TRUE(c.IsValid());
        EXPECT_NE(a.index, c.index);
        b.AddPass(
            "OwnerATouchPass", [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(a); },
            [](PassContext&) {});
        b.AddPass(
            "OwnerBTouchPass", [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(c); },
            [](PassContext&) {});
        return {};
    });

    const std::optional<DebugTextureSnapshot> snapA = fixture.GetRenderGraph().DebugTextureSnapshotFor("BuilderOwnerA::History");
    const std::optional<DebugTextureSnapshot> snapB = fixture.GetRenderGraph().DebugTextureSnapshotFor("BuilderOwnerB::History");
    ASSERT_TRUE(snapA.has_value());
    ASSERT_TRUE(snapB.has_value());
    EXPECT_NE(snapA->target.image, snapB->target.image);
}

// Debug asserts still fire when reached through the builder. The pipelined resize refusal is covered by the
// direct Resolve() test above: the builder only forwards the timing mode.
#ifndef NDEBUG

TEST(RenderGraphPersistentResourceCacheDeathTest, BuilderGetOrCreatePersistentTextureAssertsWhenDescHasDepthIsTrue)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            TextureDesc desc = MakeColorDesc();
            desc.hasDepth = true;
            PersistentTextureCacheToken token;
            fixture.GetRenderGraph().BeginPersistentResourceFrame();
            fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
                (void)b.GetOrCreatePersistentTexture(token, kTestOwner, "BuilderDepthRefusal", desc);
                return {};
            });
        },
        "");
}

// Empty owner is a deliberate escape hatch: the identity stays the literal `name`, no "<owner>::" prefix.
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureWithEmptyOwnerUsesNameAsLiteralIdentity)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    PersistentTextureCacheToken token;
    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle h = b.GetOrCreatePersistentTexture(token, "", "BuilderLiteralIdentity", desc);
        EXPECT_TRUE(h.IsValid());
        b.AddPass(
            "BuilderLiteralIdentityTouchPass",
            [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
            [](PassContext&) {});
        return {};
    });

    const std::optional<DebugTextureSnapshot> snapshot =
        fixture.GetRenderGraph().DebugTextureSnapshotFor("BuilderLiteralIdentity");
    ASSERT_TRUE(snapshot.has_value());
}

TEST(RenderGraphPersistentResourceCacheDeathTest, BuilderGetOrCreatePersistentTextureAssertsWhenTokenReusedAcrossDifferentIdentities)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) {
            GTEST_SKIP() << probe.SkipReason();
        }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            const TextureDesc desc = MakeColorDesc();
            PersistentTextureCacheToken token;
            fixture.GetRenderGraph().BeginPersistentResourceFrame();
            fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
                (void)b.GetOrCreatePersistentTexture(token, "TokenMismatchOwnerA", "TokenMismatch", desc);
                (void)b.GetOrCreatePersistentTexture(token, "TokenMismatchOwnerB", "TokenMismatch", desc);
                return {};
            });
        },
        "");
}

#endif // !NDEBUG

// desc.usage == TextureUsage::Storage is threaded into the real created RenderTexture.
TEST(RenderGraphPersistentResourceCacheTest, ResolveThreadsStorageUsageIntoTheCreatedTexture)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    TextureDesc storageDesc = MakeColorDesc();
    storageDesc.usage = TextureUsage::Storage;

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolved = cache.Resolve(kTestOwner, "StorageUsage", storageDesc, /*currentFrame=*/1, kSync);
        return {};
    });

    ASSERT_TRUE(resolved.has_value());
    ASSERT_NE(resolved->texture, nullptr);
    EXPECT_TRUE(resolved->texture->AllowsStorageImageAccess());
}

// A plain desc (TextureUsage::None) never opts into storage-image access.
TEST(RenderGraphPersistentResourceCacheTest, ResolveWithoutStorageUsageNeverAllowsStorageImageAccess)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        resolved = cache.Resolve(kTestOwner, "NoStorageUsage", desc, /*currentFrame=*/1, kSync);
        return {};
    });

    ASSERT_TRUE(resolved.has_value());
    ASSERT_NE(resolved->texture, nullptr);
    EXPECT_FALSE(resolved->texture->AllowsStorageImageAccess());
}

// A usage change on an existing entry needs a full VkImage recreation:
// treated as a caller bug, same as a format change, never silently applied.
TEST(RenderGraphPersistentResourceCacheTest, ReRequestingAnExistingEntryWithADifferentUsageIsRefused)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved = cache.Resolve(kTestOwner, "UsageRefusal", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for UsageRefusal";
        }
        return {};
    });

    TextureDesc differentUsageDesc = MakeColorDesc(32, 32);
    differentUsageDesc.usage = TextureUsage::Storage;

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> refused;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        refused = cache.Resolve(kTestOwner, "UsageRefusal", differentUsageDesc, /*currentFrame=*/2, kSync);
        return {};
    });
    EXPECT_FALSE(refused.has_value());
}

// A failed persistent texture create returns an invalid handle and logs once per token.
TEST(RenderGraphPersistentResourceCacheTest, BuilderReturnsInvalidHandleAndLogsOnceWhenCreateFails)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    const std::uint32_t impossibleExtent = fixture.UnsupportedImageExtent();
    PersistentTextureCacheToken token;
    bool created = false;
    for (int frame = 0; frame < 2; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            created = b.GetOrCreatePersistentTexture(
                token, "BuilderOwner", "Huge", MakeColorDesc(impossibleExtent, impossibleExtent)).IsValid();
            return {};
        });
    }
    ASSERT_FALSE(created) << "an image larger than maxImageDimension2D must be rejected";

    EXPECT_TRUE(token.refusalLogged);
    int errors = 0;
    for (const RecordingLogSink::Recorded& entry : sink.entries) {
        if (entry.level == LogLevel::Error && entry.message.find("could not be created") != std::string::npos) {
            ++errors;
        }
    }
    EXPECT_EQ(errors, 1);
}

// Storage usage on a format no device can store to is refused, logged once, and creates no entry.
TEST(RenderGraphPersistentResourceCacheTest, StorageUsageOnANonStorageFormatIsRefusedAndLoggedOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    TextureDesc desc = MakeColorDesc();
    desc.format = PixelFormat::D32_Float;
    desc.usage = TextureUsage::Storage;

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> first;
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> second;
    EXPECT_NO_THROW({
        first = cache.Resolve(kTestOwner, "NoStorageFormat", desc, 1, kSync);
        second = cache.Resolve(kTestOwner, "NoStorageFormat", desc, 2, kSync);
    });

    EXPECT_FALSE(first.has_value());
    EXPECT_FALSE(second.has_value());
    EXPECT_FALSE(cache.FramesUntilEviction(kTestOwner, "NoStorageFormat", 2).has_value());
    EXPECT_EQ(CountByCategory(sink, kCacheLogCategory), 1);
}

// A resize swaps the image behind a persistent texture; its debug snapshot must follow to the new image.
TEST(RenderGraphPersistentResourceCacheTest, DebugSnapshotFollowsThePersistentTextureAfterAResize)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const std::string combinedKey = std::string(kTestOwner) + "::SnapFollowsResize";
    PersistentTextureCacheToken token;
    const auto runFrame = [&](const TextureDesc& desc) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h = b.GetOrCreatePersistentTexture(token, kTestOwner, "SnapFollowsResize", desc);
            b.AddPass(
                "SnapFollowsResizeWrite",
                [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
                [](PassContext&) {});
            return {};
        });
        return fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
    };

    const std::optional<DebugTextureSnapshot> before = runFrame(MakeColorDesc(64, 64));
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->target.extent.width, 64u);

    const std::optional<DebugTextureSnapshot> afterResize = runFrame(MakeColorDesc(128, 128));
    ASSERT_TRUE(afterResize.has_value());
    EXPECT_EQ(afterResize->target.extent.width, 128u);
    EXPECT_NE(afterResize->target.image, before->target.image);

    const std::optional<DebugTextureSnapshot> steady = runFrame(MakeColorDesc(128, 128));
    ASSERT_TRUE(steady.has_value());
    EXPECT_EQ(steady->target.image, afterResize->target.image);
}

} // namespace gte::rg
