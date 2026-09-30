// editor-core-separation-27 campaign, PHASE4
// (PHASE4_HEADLESS_TEST_FIXTURE_AND_CACHE_CONSTRUCTION.md) - Tier-2 (real,
// headless GPU) tests for RenderGraphPersistentResourceCache's own
// construction/ownership/exception-safety core (source document Section 6,
// 6.1, 6.2, 7). GTEST_SKIP()-guarded exactly like every other
// HeadlessSurfaceProvider consumer in this codebase - see
// tests/Fakes/HeadlessRenderGraphFixture.h's own top-of-file comment.
//
// PHASE5 (PHASE5_CACHE_AGE_TRACKING_DOUBLE_REQUEST_GUARD_AND_EVICTION.md)
// EXTENDS this SAME file (never a second one) with age-tracking/eviction/the
// same-real-frame double-request guard/the debug-only token misuse guard
// tests - see the bottom half of this file. Every PHASE4 Resolve() call site
// above grew a new, mandatory trailing `currentFrame` argument (Resolve()'s
// own signature grew this parameter this same phase - no other production
// caller existed yet, so this was safe to change freely, per this phase's
// own .md Step 3.1).
//
// Scope: PHASE4's own construction/collision-safety/exception-safety/
// color-only acceptance criteria, PLUS PHASE5's own age-tracking/
// double-request-guard/eviction/debug-misuse-guard acceptance criteria.
// Resize (PHASE6) and honest-layout-recording (PHASE8) are still later
// phases of this same campaign, extending this SAME class further - none of
// that is attempted here.

#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"
#include "Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include "Core/LogSink.h" // PHASE5 - RecordingLogSink, for the double-request-refusal
    // logged-exactly-once regression test below.
#include "Editor/Logger.h" // PHASE5 - LoggerLogSink::Instance(), to restore the real sink
    // afterward - mirrors Core/LogSinkTests.cpp's own precedent exactly.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte::rg {
namespace {

// A fresh, color-only (hasDepth == false) TextureDesc - every test below
// that doesn't need a specific size/format uses this.
TextureDesc MakeColorDesc(std::uint32_t width = 64, std::uint32_t height = 64)
{
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.hasDepth = false;
    return desc;
}

// PHASE5 - a small, test-only ILogSink recording every call verbatim,
// mirroring Core/LogSinkTests.cpp's own RecordingLogSink precedent exactly.
class RecordingLogSink : public ILogSink {
public:
    struct Recorded {
        LogLevel level;
        std::string category;
        std::string message;
    };

    void Log(LogLevel level, std::string_view category, std::string_view message) override
    {
        entries.push_back(Recorded{ level, std::string(category), std::string(message) });
    }

    std::vector<Recorded> entries;
};

// PHASE5 - RAII guard restoring the REAL sink (gte::LoggerLogSink::Instance())
// on scope exit regardless of pass/fail/early-return, so a test installing a
// fake sink to observe GTE_LOG_ERROR never leaves a later test in this SAME
// binary talking to a stale/fake sink - mirrors Core/LogSinkTests.cpp's own
// TearDown() precedent, adapted to a plain TEST() (no fixture class) here.
class ScopedLogSinkInstall {
public:
    explicit ScopedLogSinkInstall(ILogSink* sink) { InstallLogSink(sink); }
    ~ScopedLogSinkInstall() { InstallLogSink(&LoggerLogSink::Instance()); }
};

} // namespace

// 1. Basic construct-and-reuse - the single most important proof in this
// phase: Resolve() called across three SEPARATE, real, driven Execute()
// frames returns the SAME RenderTexture* / same underlying VkImage every
// time.
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
            resolved = cache.Resolve(
                kPersistentOwnerCacheValidation, "BasicReuse", desc, static_cast<std::uint64_t>(frame + 1));
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

// 2. Two owners, same name, same desc, never collide - the concrete
// regression test for Section 6.2 population (a)'s collision-safety
// guarantee (the single most important NEW test the source document itself
// calls out, Section 12).
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
        resolvedA = cache.Resolve("OwnerA", "History", desc, /*currentFrame=*/1);
        resolvedB = cache.Resolve("OwnerB", "History", desc, /*currentFrame=*/1);
        return {};
    });

    ASSERT_TRUE(resolvedA.has_value());
    ASSERT_TRUE(resolvedB.has_value());
    ASSERT_NE(resolvedA->texture, nullptr);
    ASSERT_NE(resolvedB->texture, nullptr);
    EXPECT_NE(resolvedA->texture, resolvedB->texture);
    EXPECT_NE(resolvedA->texture->Image(), resolvedB->texture->Image());
}

// 3. Exception safety - genuinely hard to force a real vmaCreateImage()
// failure on demand, so this confirms the STRUCTURAL guarantee directly: a
// deliberately huge (but positive, see this phase's own .md for why
// UINT32_MAX is the wrong choice) desc that IS expected to fail on real
// hardware throws (never silently returns a broken handle), and an
// immediate retry with a sane desc for the SAME (owner, name) succeeds
// cleanly afterward (the failed placeholder did not permanently poison that
// key). Honestly documented, machine-dependent limitation: if this
// machine's driver/GPU somehow accepts a 100000x100000 image, this test
// SKIPS rather than fabricating a false-positive pass - see this phase's
// own completion report for what actually happened on this dev machine.
TEST(RenderGraphPersistentResourceCacheTest, FailedResolveThrowsAndLeavesTheKeyRetryableAfterward)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc hugeDesc = MakeColorDesc(100000u, 100000u);

    bool threw = false;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        try {
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "ExceptionSafety", hugeDesc, /*currentFrame=*/1);
        } catch (const std::exception&) {
            threw = true;
        }
        return {};
    });

    if (!threw) {
        GTEST_SKIP() << "This machine's Vulkan driver/GPU did not reject a 100000x100000 image allocation as "
                        "expected - cannot exercise the exception-safety path here on this hardware (see this "
                        "phase's own .md, Step 4 item 3, for why this is a documented, honest, machine-dependent "
                        "limitation rather than a fabricated pass).";
    }

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> retryResolved;
    const TextureDesc saneDesc = MakeColorDesc();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        retryResolved = cache.Resolve(kPersistentOwnerCacheValidation, "ExceptionSafety", saneDesc, /*currentFrame=*/2);
        return {};
    });

    ASSERT_TRUE(retryResolved.has_value());
    EXPECT_NE(retryResolved->texture, nullptr);
}

// 4. desc.hasDepth == true / null / empty owner / null / empty name - each
// guarded by a plain assert() immediately before its GTE_LOG_ERROR() +
// std::nullopt fallback (see RenderGraphPersistentResourceCache.cpp's
// Resolve()) - this repo's normal cmake --build build + ctest cycle never
// defines NDEBUG (confirmed, see this phase's own .md), so assert() stays
// live and each of these ABORTS THE WHOLE PROCESS rather than gracefully
// returning std::nullopt - exactly mirroring RenderGraphBuilderTests.cpp's
// own WriteColorAttachmentExceedingCapAssertsInDebug precedent. The
// release-build (NDEBUG defined), log-and-refuse std::nullopt-returning
// half of this behavior is NOT exercised by this project's normal ctest run
// at all - the same accepted, disclosed scope limit that precedent already
// lives with.
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "DepthRefusal", desc, /*currentFrame=*/1);
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
            (void)cache.Resolve(nullptr, "NullOwnerRefusal", desc, /*currentFrame=*/1);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, nullptr, desc, /*currentFrame=*/1);
        },
        "");
}

TEST(RenderGraphPersistentResourceCacheDeathTest, ResolveAssertsOnEmptyOwner)
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
            (void)cache.Resolve("", "EmptyOwnerRefusal", desc, /*currentFrame=*/1);
        },
        "");
}

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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "", desc, /*currentFrame=*/1);
        },
        "");
}

#endif // !NDEBUG

// 5. No depth companion allocated - the concrete regression test for
// Section 7's createDepthCompanion requirement. Both RenderTexture's own
// color image AND DepthBuffer register as GpuResourceType::Texture
// (confirmed: RenderTexture.cpp/DepthBuffer.cpp both call
// tracker->Track(GpuResourceType::Texture, ...)), so a textureCount delta
// of exactly 1 (never 2) across this call proves no depth companion was
// silently still allocated.
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "NoDepthCheck", desc, /*currentFrame=*/1);
        return {};
    });

    ASSERT_TRUE(resolved.has_value());

    const GpuMemoryTracker::Totals after = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(after.textureCount, before.textureCount + 1);
}

// 6. A dozen distinct identities, container growth - the concrete
// regression test for TR4's node-stability requirement: std::unordered_map
// never invalidates an existing node's address/reference on insertion of
// new keys (unlike a std::vector, which would relocate every existing
// element on growth, silently dangling every RenderTexture::m_debugName
// pointer already handed out).
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
        VkExtent2D extent{};
    };

    std::vector<Recorded> recorded;
    constexpr int kIdentityCount = 12;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        for (int i = 0; i < kIdentityCount; ++i) {
            const std::string name = "Identity" + std::to_string(i);
            const TextureDesc desc =
                MakeColorDesc(32u + static_cast<std::uint32_t>(i), 32u + static_cast<std::uint32_t>(i));
            std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
                cache.Resolve(kPersistentOwnerCacheValidation, name.c_str(), desc, /*currentFrame=*/1);
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

    // Re-resolve every identity again inside a SECOND, separate real frame -
    // every EARLIER pointer/handle/extent must still be the exact same,
    // correctly-sized resource.
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        for (const Recorded& entry : recorded) {
            const TextureDesc desc = MakeColorDesc(entry.extent.width, entry.extent.height);
            std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
                cache.Resolve(kPersistentOwnerCacheValidation, entry.name.c_str(), desc, /*currentFrame=*/2);
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

// --- PHASE5 (PHASE5_CACHE_AGE_TRACKING_DOUBLE_REQUEST_GUARD_AND_EVICTION.md) ---

// 7. Eviction, basic - an entry nobody has requested for more than
// staleThresholdFrames real frames is destroyed and its GPU memory freed on
// the next BeginFrame() sweep that walks past that threshold. Uses a SMALL
// threshold passed directly to BeginFrame() (never the real 300-frame
// default), so this test never has to actually iterate 300 real frames.
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
            cache.Resolve(kPersistentOwnerCacheValidation, "EvictMe", desc, /*currentFrame=*/1);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Resolve() unexpectedly failed for EvictMe";
        }
        return {};
    });
    cache.BeginFrame(1, kSmallThreshold); // elapsed 0 - never evicted the very frame it was created.

    const GpuMemoryTracker::Totals afterCreate = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(afterCreate.textureCount, before.textureCount + 1);
    EXPECT_TRUE(cache.FramesUntilEviction(kPersistentOwnerCacheValidation, "EvictMe", 1).has_value());

    // Walk the frame counter forward with NO further Resolve() call for this
    // identity - lastUsedFrame stays pinned at 1.
    cache.BeginFrame(2, kSmallThreshold); // elapsed 1, not yet stale (threshold is 2).
    EXPECT_TRUE(cache.FramesUntilEviction(kPersistentOwnerCacheValidation, "EvictMe", 2).has_value());

    cache.BeginFrame(4, kSmallThreshold); // elapsed 3 > 2 - evicted this sweep.
    EXPECT_FALSE(cache.FramesUntilEviction(kPersistentOwnerCacheValidation, "EvictMe", 4).has_value());

    const GpuMemoryTracker::Totals afterEvict = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(afterEvict.textureCount, before.textureCount);
}

// 8. Eviction does not fire while still in active use - the direct
// regression test for "an entry requested every real frame always
// evaluates as zero-or-one frames idle at sweep time, never accidentally
// evicted mid-use".
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
                cache.Resolve(kPersistentOwnerCacheValidation, "AlwaysActive", desc, frame);
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

    EXPECT_TRUE(cache.FramesUntilEviction(kPersistentOwnerCacheValidation, "AlwaysActive", 10).has_value());
}

// 9. FramesUntilEviction()'s two overloads agree with each other.
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "AgreeCheck", desc, /*currentFrame=*/1);
        return {};
    });
    ASSERT_TRUE(resolved.has_value());
    ASSERT_NE(resolved->combinedKey, nullptr);

    const std::optional<std::uint64_t> byCombined = cache.FramesUntilEviction(*resolved->combinedKey, 5);
    const std::optional<std::uint64_t> byOwnerName = cache.FramesUntilEviction(kPersistentOwnerCacheValidation, "AgreeCheck", 5);
    ASSERT_TRUE(byCombined.has_value());
    ASSERT_TRUE(byOwnerName.has_value());
    EXPECT_EQ(*byCombined, *byOwnerName);
}

// 10. Double-request refusal, order A - within ONE simulated frame value,
// requesting the SAME identity twice must succeed the first time and
// refuse (std::nullopt) every subsequent time that same frame, logging the
// GTE_LOG_ERROR exactly once ever for this identity - never once per
// refused call. Order B (the reverse call order) is meaningless for THIS
// phase's own tests, since Resolve() itself has no concept of which
// ExecuteTimingMode regime called it - see this phase's own .md, Step 4
// item 5, for why a fabricated "regime order" test is deliberately NOT
// added here.
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
        first = cache.Resolve(kPersistentOwnerCacheValidation, "Dup", desc, /*currentFrame=*/5);
        second = cache.Resolve(kPersistentOwnerCacheValidation, "Dup", desc, /*currentFrame=*/5);
        third = cache.Resolve(kPersistentOwnerCacheValidation, "Dup", desc, /*currentFrame=*/5);
        return {};
    });

    ASSERT_TRUE(first.has_value());
    EXPECT_FALSE(second.has_value());
    EXPECT_FALSE(third.has_value());

    int matchingErrorCount = 0;
    for (const RecordingLogSink::Recorded& entry : sink.entries) {
        if (entry.category == "RenderGraphPersistentResourceCache" && entry.level == LogLevel::Error
            && entry.message.find("Dup") != std::string::npos) {
            ++matchingErrorCount;
        }
    }
    EXPECT_EQ(matchingErrorCount, 1);
}

// 11. A request in a LATER frame after a same-frame refusal succeeds
// normally, returning the SAME underlying RenderTexture* - proves a
// same-frame refusal never poisons the entry for future frames.
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
        frame5First = cache.Resolve(kPersistentOwnerCacheValidation, "LaterFrameRetry", desc, /*currentFrame=*/5);
        frame5Second = cache.Resolve(kPersistentOwnerCacheValidation, "LaterFrameRetry", desc, /*currentFrame=*/5);
        return {};
    });
    ASSERT_TRUE(frame5First.has_value());
    EXPECT_FALSE(frame5Second.has_value());

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> frame6;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        frame6 = cache.Resolve(kPersistentOwnerCacheValidation, "LaterFrameRetry", desc, /*currentFrame=*/6);
        return {};
    });
    ASSERT_TRUE(frame6.has_value());
    EXPECT_EQ(frame6->texture, frame5First->texture);
}

// 12. Stale-token safety (the accepted-risk test, this phase's own
// "Situation" section requirement): resolve an identity, capture its
// entry/epoch, evict it (small threshold + BeginFrame()), THEN resolve a
// DIFFERENT, brand-new identity (to encourage, though never guarantee, the
// allocator reusing the freed node), and finally call IsTokenLive() with
// the ORIGINAL (now-stale) entry pointer/epoch - must return false and,
// most importantly, must not crash. This engine's normal cmake --build
// build is NOT compiled with AddressSanitizer/UndefinedBehaviorSanitizer
// (confirmed: no -fsanitize= flag anywhere in this project's own
// CMakeLists.txt - only third_party/ktx's vendored, unrelated basisu
// sub-build opts into it for ITS OWN code) - this is disclosed here
// plainly, per this phase's own .md instruction, rather than silently
// assumed available.
TEST(RenderGraphPersistentResourceCacheTest, IsTokenLiveReturnsFalseForAnEvictedEntryWithoutCrashing)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    const TextureDesc desc = MakeColorDesc();
    constexpr std::uint64_t kSmallThreshold = 1;

    const PersistentResourceCacheEntry* staleEntry = nullptr;
    std::uint64_t staleEpoch = 0;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
            cache.Resolve(kPersistentOwnerCacheValidation, "StaleToken", desc, /*currentFrame=*/1);
        if (resolved.has_value()) {
            staleEntry = resolved->entry;
            staleEpoch = resolved->entryEpoch;
        }
        return {};
    });

    ASSERT_NE(staleEntry, nullptr);
    EXPECT_TRUE(cache.IsTokenLive(staleEntry, staleEpoch));

    // Walk the frame counter far enough forward, with no further Resolve()
    // call for this identity, to evict it.
    cache.BeginFrame(1, kSmallThreshold);
    cache.BeginFrame(10, kSmallThreshold);
    ASSERT_FALSE(cache.FramesUntilEviction(kPersistentOwnerCacheValidation, "StaleToken", 10).has_value());

    // Resolve a DIFFERENT, brand-new identity - encourages (never
    // guarantees) the allocator reusing the freed unordered_map node.
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "AfterEviction", desc, /*currentFrame=*/10);
        return {};
    });

    // The stale token must report NOT live, and this call itself must not
    // crash/UB-sanitizer-flag anything (see this test's own header comment
    // for this build's real ASan/UBSan availability status).
    EXPECT_FALSE(cache.IsTokenLive(staleEntry, staleEpoch));
}

// 13. DebugTokenIdentityMatches() - debug builds only (mirrors the
// production code's own #ifndef NDEBUG guard exactly, so a release-
// configured ctest run does not fail to find symbols that don't exist in
// that build).
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "IdentityCheck", desc, /*currentFrame=*/1);
        return {};
    });
    ASSERT_TRUE(resolved.has_value());

    EXPECT_TRUE(cache.DebugTokenIdentityMatches(resolved->entry, kPersistentOwnerCacheValidation, "IdentityCheck"));
    EXPECT_FALSE(cache.DebugTokenIdentityMatches(resolved->entry, "SomeoneElse", "IdentityCheck"));
    EXPECT_TRUE(cache.DebugTokenIdentityMatches(nullptr, "Anything", "Anything"));
}
#endif // !NDEBUG

} // namespace gte::rg
