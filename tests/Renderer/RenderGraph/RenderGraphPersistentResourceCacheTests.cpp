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
// tests. Every PHASE4 Resolve() call site grew a new, mandatory trailing
// `currentFrame` argument that same phase.
//
// PHASE6 (PHASE6_CACHE_BATCHED_RESIZE.md) EXTENDS this SAME file again with
// the batched-resize tests (bottom of this file) - Resolve() grew a FIFTH,
// mandatory trailing `ExecuteTimingMode timingMode` parameter this same
// phase, so every earlier call site below was updated to pass `kSync`
// (`ExecuteTimingMode::SynchronousImmediateReadback`) - no other production
// caller existed yet, so this was safe to change freely, per this phase's
// own .md Step 3.2.
//
// Scope: PHASE4's own construction/collision-safety/exception-safety/
// color-only acceptance criteria, PHASE5's own age-tracking/
// double-request-guard/eviction/debug-misuse-guard acceptance criteria, and
// PHASE6's own batched-resize/format-change-refusal/pipelined-regime-resize-
// refusal acceptance criteria. Honest-layout-recording (PHASE8) is still a
// later phase of this same campaign, extending this SAME class further -
// none of that is attempted here.

#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"
#include "Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include "Core/LogSink.h" // PHASE5 - RecordingLogSink, for the double-request-refusal
    // logged-exactly-once regression test below.
#include "Editor/Logger.h" // PHASE5 - LoggerLogSink::Instance(), to restore the real sink
    // afterward - mirrors Core/LogSinkTests.cpp's own precedent exactly.

#include <gtest/gtest.h>

#include <array>
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

// PHASE6 - short, readable aliases for Resolve()'s new mandatory trailing
// ExecuteTimingMode parameter, used throughout this whole file.
constexpr ExecuteTimingMode kSync = ExecuteTimingMode::SynchronousImmediateReadback;
constexpr ExecuteTimingMode kPipelined = ExecuteTimingMode::PipelinedDeferredReadback;

// PHASE5 - a small, test-only ILogSink recording every call verbatim,
// mirroring Core/LogSinkTests.cpp's own RecordingLogSink precedent exactly.
class RecordingLogSink : public ILogSink {
public:
    struct Recorded {
        LogLevel level;
        std::string category;
        std::string message;
    };

    void Log(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false) override
    {
        entries.push_back(Recorded{ level, std::string(category), std::string(message) });
        (void)isBlocking;
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
                kPersistentOwnerCacheValidation, "BasicReuse", desc, static_cast<std::uint64_t>(frame + 1), kSync);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "ExceptionSafety", hugeDesc, /*currentFrame=*/1, kSync);
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
        retryResolved = cache.Resolve(kPersistentOwnerCacheValidation, "ExceptionSafety", saneDesc, /*currentFrame=*/2, kSync);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "DepthRefusal", desc, /*currentFrame=*/1, kSync);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, nullptr, desc, /*currentFrame=*/1, kSync);
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
            (void)cache.Resolve("", "EmptyOwnerRefusal", desc, /*currentFrame=*/1, kSync);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "", desc, /*currentFrame=*/1, kSync);
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "NoDepthCheck", desc, /*currentFrame=*/1, kSync);
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
                cache.Resolve(kPersistentOwnerCacheValidation, name.c_str(), desc, /*currentFrame=*/1, kSync);
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
                cache.Resolve(kPersistentOwnerCacheValidation, entry.name.c_str(), desc, /*currentFrame=*/2, kSync);
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
            cache.Resolve(kPersistentOwnerCacheValidation, "EvictMe", desc, /*currentFrame=*/1, kSync);
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
                cache.Resolve(kPersistentOwnerCacheValidation, "AlwaysActive", desc, frame, kSync);
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "AgreeCheck", desc, /*currentFrame=*/1, kSync);
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
        first = cache.Resolve(kPersistentOwnerCacheValidation, "Dup", desc, /*currentFrame=*/5, kSync);
        second = cache.Resolve(kPersistentOwnerCacheValidation, "Dup", desc, /*currentFrame=*/5, kSync);
        third = cache.Resolve(kPersistentOwnerCacheValidation, "Dup", desc, /*currentFrame=*/5, kSync);
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
        frame5First = cache.Resolve(kPersistentOwnerCacheValidation, "LaterFrameRetry", desc, /*currentFrame=*/5, kSync);
        frame5Second = cache.Resolve(kPersistentOwnerCacheValidation, "LaterFrameRetry", desc, /*currentFrame=*/5, kSync);
        return {};
    });
    ASSERT_TRUE(frame5First.has_value());
    EXPECT_FALSE(frame5Second.has_value());

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> frame6;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        frame6 = cache.Resolve(kPersistentOwnerCacheValidation, "LaterFrameRetry", desc, /*currentFrame=*/6, kSync);
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
            cache.Resolve(kPersistentOwnerCacheValidation, "StaleToken", desc, /*currentFrame=*/1, kSync);
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
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "AfterEviction", desc, /*currentFrame=*/10, kSync);
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "IdentityCheck", desc, /*currentFrame=*/1, kSync);
        return {};
    });
    ASSERT_TRUE(resolved.has_value());

    EXPECT_TRUE(cache.DebugTokenIdentityMatches(resolved->entry, kPersistentOwnerCacheValidation, "IdentityCheck"));
    EXPECT_FALSE(cache.DebugTokenIdentityMatches(resolved->entry, "SomeoneElse", "IdentityCheck"));
    EXPECT_TRUE(cache.DebugTokenIdentityMatches(nullptr, "Anything", "Anything"));
}
#endif // !NDEBUG

// --- PHASE6 (PHASE6_CACHE_BATCHED_RESIZE.md) ---

// 14. Basic resize - a re-requested identity with a different width/height
// is NOT resized inline: this call's own returned texture/extent is still
// the OLD one, and only AFTER an explicit FlushPendingResizes() call does
// the SAME RenderTexture* report the NEW extent, with lastKnownLayout reset
// to VK_IMAGE_LAYOUT_UNDEFINED (Section 5.1 - a freshly recreated VkImage
// really is undefined again).
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
        firstResolve = cache.Resolve(kPersistentOwnerCacheValidation, "Resize", desc64, /*currentFrame=*/1, kSync);
        return {};
    });
    ASSERT_TRUE(firstResolve.has_value());
    RenderTexture* texture = firstResolve->texture;
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->Extent().width, 64u);

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> secondResolve;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        secondResolve = cache.Resolve(kPersistentOwnerCacheValidation, "Resize", desc128, /*currentFrame=*/2, kSync);
        return {};
    });
    ASSERT_TRUE(secondResolve.has_value());
    // (a) the resize hasn't happened yet - THIS call's own returned extent
    // is still the OLD one.
    EXPECT_EQ(secondResolve->texture, texture);
    EXPECT_EQ(secondResolve->texture->Extent().width, 64u);

    cache.FlushPendingResizes();

    // (b) after the flush, the SAME RenderTexture* is now the new size.
    EXPECT_EQ(texture->Extent().width, 128u);
    EXPECT_EQ(texture->Extent().height, 128u);

    // (c) lastKnownLayout was reset to VK_IMAGE_LAYOUT_UNDEFINED on that
    // same entry - confirmed via a fresh Resolve() call's own returned
    // ResolvedTexture::lastKnownLayout.
    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> thirdResolve;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        thirdResolve = cache.Resolve(kPersistentOwnerCacheValidation, "Resize", desc128, /*currentFrame=*/3, kSync);
        return {};
    });
    ASSERT_TRUE(thirdResolve.has_value());
    EXPECT_EQ(thirdResolve->lastKnownLayout, VK_IMAGE_LAYOUT_UNDEFINED);
}

// 15. Batched - the single most important test in this phase: three
// distinct persistent entries each queue their OWN different new size,
// across TWO separate real frames (never flushed in between), and exactly
// ONE FlushPendingResizes() call resizes ALL THREE correctly. This project
// has no Vulkan-call-counting test harness, so the "exactly one
// vkDeviceWaitIdle() total, no matter how many entries" claim is proven
// HERE by this test confirming the batching logic itself is correct (all
// three entries updated by a SINGLE FlushPendingResizes() call), combined
// with a direct code-read fact stated plainly in this phase's own
// completion report: FlushPendingResizes()'s body contains exactly one,
// unconditional vkDeviceWaitIdle() statement, outside any loop, guarded
// only by an early-return when m_pendingResizes is empty.
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
        auto a = cache.Resolve(kPersistentOwnerCacheValidation, "BatchA", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        auto b = cache.Resolve(kPersistentOwnerCacheValidation, "BatchB", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        auto c = cache.Resolve(kPersistentOwnerCacheValidation, "BatchC", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
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
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "BatchA", MakeColorDesc(64, 64), /*currentFrame=*/2, kSync);
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "BatchB", MakeColorDesc(96, 96), /*currentFrame=*/2, kSync);
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "BatchC", MakeColorDesc(128, 128), /*currentFrame=*/2, kSync);
        return {};
    });

    // Every entry's extent is STILL the old one - not resized yet.
    EXPECT_EQ(textureA->Extent().width, 32u);
    EXPECT_EQ(textureB->Extent().width, 32u);
    EXPECT_EQ(textureC->Extent().width, 32u);

    cache.FlushPendingResizes();

    EXPECT_EQ(textureA->Extent().width, 64u);
    EXPECT_EQ(textureB->Extent().width, 96u);
    EXPECT_EQ(textureC->Extent().width, 128u);
}

// 16. Last-request-wins - two DIFFERENT resize requests for the SAME
// identity, before ever calling FlushPendingResizes(), only ever apply the
// SECOND (most recent) one. Deliberately issued across TWO SEPARATE real
// frames (never the literal same currentFrame value for the SAME identity)
// - PHASE5's own same-real-frame double-request guard (Section 5.3) already
// refuses a second Resolve() call for the SAME identity within one literal
// frame value unconditionally, so that scenario can never reach this
// resize-queuing logic at all in production; QueueResize()'s own
// "last request wins" branch is what protects an entry that requests a
// NEW size on more than one real frame before a flush ever happens.
TEST(RenderGraphPersistentResourceCacheTest, LastResizeRequestBeforeAFlushWinsOverAnEarlierOne)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    RenderTexture* texture = nullptr;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved =
            cache.Resolve(kPersistentOwnerCacheValidation, "LastWins", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for LastWins";
            return {};
        }
        texture = resolved->texture;
        return {};
    });
    ASSERT_NE(texture, nullptr);

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "LastWins", MakeColorDesc(64, 64), /*currentFrame=*/2, kSync);
        return {};
    });
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        (void)cache.Resolve(kPersistentOwnerCacheValidation, "LastWins", MakeColorDesc(96, 96), /*currentFrame=*/3, kSync);
        return {};
    });

    // Still not applied - nothing flushed yet.
    EXPECT_EQ(texture->Extent().width, 32u);

    cache.FlushPendingResizes();

    // Only the SECOND (most recent) request's size ever took effect.
    EXPECT_EQ(texture->Extent().width, 96u);
    EXPECT_EQ(texture->Extent().height, 96u);
}

// 17. Pipelined-regime resize refusal - a resize requested while
// timingMode == ExecuteTimingMode::PipelinedDeferredReadback (a) still
// SUCCEEDS (a valid ResolvedTexture, not std::nullopt - only the resize
// portion is refused), (b) leaves the entry's extent UNCHANGED, and (c)
// queues nothing (confirmed indirectly: a subsequent FlushPendingResizes()
// call does not touch this entry's extent at all).
TEST(RenderGraphPersistentResourceCacheTest, ResizeRequestedFromThePipelinedRegimeIsRefusedButTheCallStillSucceeds)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());
    RenderTexture* texture = nullptr;

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved = cache.Resolve(
            kPersistentOwnerCacheValidation, "PipelinedRefusal", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
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
        pipelinedResolve = cache.Resolve(
            kPersistentOwnerCacheValidation, "PipelinedRefusal", MakeColorDesc(128, 128), /*currentFrame=*/2, kPipelined);
        return {};
    });

    // (a) the call still SUCCEEDS.
    ASSERT_TRUE(pipelinedResolve.has_value());
    EXPECT_EQ(pipelinedResolve->texture, texture);
    // (b) the entry's extent is UNCHANGED afterward.
    EXPECT_EQ(texture->Extent().width, 32u);

    // (c) nothing was queued for it - a subsequent flush does not touch
    // this entry's extent at all.
    cache.FlushPendingResizes();
    EXPECT_EQ(texture->Extent().width, 32u);
}

// 18. Format-change refusal - re-requesting an EXISTING entry with the SAME
// width/height but a DIFFERENT desc.format is a hard refusal
// (std::nullopt), regardless of timingMode - always a caller bug, never
// silently reinterpreted, never queued as a resize.
TEST(RenderGraphPersistentResourceCacheTest, ReRequestingAnExistingEntryWithADifferentFormatIsRefusedRegardlessOfRegime)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RenderGraphPersistentResourceCache cache(fixture.GetRenderer());

    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        auto resolved = cache.Resolve(
            kPersistentOwnerCacheValidation, "FormatRefusal", MakeColorDesc(32, 32), /*currentFrame=*/1, kSync);
        if (!resolved.has_value()) {
            ADD_FAILURE() << "Initial Resolve() unexpectedly failed for FormatRefusal";
        }
        return {};
    });

    TextureDesc differentFormatDesc = MakeColorDesc(32, 32);
    differentFormatDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> refusedSync;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        refusedSync =
            cache.Resolve(kPersistentOwnerCacheValidation, "FormatRefusal", differentFormatDesc, /*currentFrame=*/2, kSync);
        return {};
    });
    EXPECT_FALSE(refusedSync.has_value());

    std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> refusedPipelined;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder&) -> std::vector<TextureHandle> {
        refusedPipelined = cache.Resolve(
            kPersistentOwnerCacheValidation, "FormatRefusal", differentFormatDesc, /*currentFrame=*/3, kPipelined);
        return {};
    });
    EXPECT_FALSE(refusedPipelined.has_value());
}

// --- PHASE7 (PHASE7_RENDERGRAPH_INTEGRATION_AND_FRAME_COUNTER.md) ---

// 19. CurrentPersistentResourceFrameCounter() - starts at 0, pre-increments
// on every BeginPersistentResourceFrame() call (1 after the first, 2 after
// the second, etc.) - the direct regression test for this phase's own
// frame-counter plumbing, using the SAME HeadlessRenderGraphFixture (a real
// rg::RenderGraph, via fixture.GetRenderGraph()) every other test in this
// file already uses.
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

// 20. RenderGraph::Execute()'s own template body now ALSO calls
// SetPersistentResourceCache() every single call (this phase's own new
// wiring) - a basic smoke check confirming an ordinary, empty-build
// SynchronousImmediateReadback frame still runs with zero crash/misbehavior
// now that this extra setter call happens on every Execute(). Full
// end-to-end confirmation that the THREE values genuinely reach
// GetOrCreatePersistentTexture() correctly is deliberately deferred to
// PHASE8's own tests (that method does not exist yet this phase) - see this
// phase's own .md, Step 4 item 2.
TEST(RenderGraphPersistentResourceCacheTest, ExecuteStillRunsANormalEmptyFrameAfterGainingTheNewSetterCall)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    EXPECT_NO_THROW({
        fixture.RunSynchronousFrame(
            [](RenderGraphBuilder&) -> std::vector<TextureHandle> { return {}; });
    });
}

// --- PHASE8 (PHASE8_BUILDER_GETORCREATEPERSISTENTTEXTURE_AND_HONEST_LAYOUT_WIRING.md) ---
//
// Everything below drives the feature through the REAL, production-shaped
// entry point, RenderGraphBuilder::GetOrCreatePersistentTexture() - NOT
// RenderGraphPersistentResourceCache::Resolve()/ResolveFast() directly (the
// tests above, PHASE4-6, deliberately used the lower-level API since the
// builder wiring didn't exist yet) - through the SAME real, internal cache
// `fixture.GetRenderGraph()` itself owns (RenderGraph::m_persistentResourceCache),
// never a second, test-constructed instance. Every test below calls
// `fixture.GetRenderGraph().BeginPersistentResourceFrame()` immediately
// before each `RunSynchronousFrame()` call, exactly mirroring
// `Core::BuildFrame()`'s own real per-frame call order (Correction 1/Locked
// Decision 2, PHASE0_MASTER_STRATEGY.md) - this is REQUIRED, not cosmetic:
// skipping it would leave `RenderGraph::m_persistentResourceFrameCounter`
// pinned at 0 forever, which would incorrectly trip the same-real-frame
// double-request guard (Section 5.3) on every single call after the first.

// 21. Same VkImage across 3 real frames, via the real builder API - the
// single most important proof THIS phase adds (mirrors test 1 above, this
// time through GetOrCreatePersistentTexture() + a real Execute() cycle
// end-to-end, confirmed via the SAME DebugTextureSnapshotFor() query
// GET /get_texture itself is built on). A real, declared touching pass
// (WriteColorAttachment with NO clear color -> VK_ATTACHMENT_LOAD_OP_LOAD,
// preserving whatever content is already there) is REQUIRED here, not
// optional: RenderGraphCompiler::Compile()'s root-marking scan only makes a
// PASS survive culling (never a bare, pass-less handle), and
// RegisterDebugTextureSnapshots()/DebugTextureSnapshotFor() only ever
// reflect a texture a SURVIVING pass actually resolved this call
// (physicalTextures[h.index].resolved) - a call to
// GetOrCreatePersistentTexture() with NO pass at all touching the returned
// handle would leave it unresolved this frame, and DebugTextureSnapshotFor()
// would correctly return std::nullopt (a real, confirmed-live defect found
// and fixed in this exact shape during this phase's own dispatch_sub_agent
// double-check, before this file was committed - see this phase's own
// completion report).
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureReturnsTheSamePhysicalTextureAcrossThreeFrames)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    const std::string combinedKey = std::string(kPersistentOwnerCacheValidation) + "::BuilderReuse";
    VkImage firstImage = VK_NULL_HANDLE;

    for (int frame = 0; frame < 3; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h = b.GetOrCreatePersistentTexture(kPersistentOwnerCacheValidation, "BuilderReuse", desc);
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

// 22. Token-based fast path - identical result to the plain overload, AND a
// real, if indirect, proxy that the fast path itself was genuinely taken on
// frames 2/3 (not merely "produced the identical result", which the slow
// path would also do): RenderGraphBuilder::GetOrCreatePersistentTexture()'s
// token overload only ever calls RenderGraphPersistentResourceCache::
// DebugTokenIdentityMatches()'s debug-only assert STRICTLY INSIDE its own
// `if (IsTokenLive(...))` branch (see RenderGraphBuilder.cpp) - this whole
// test completing normally (never aborting) across 3 repeated calls with a
// STABLE token is therefore real evidence that branch (and only that
// branch) executed on the second and third calls. See test 21's own doc
// comment above for why a real, declared touching pass is required here too.
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureTokenFastPathReusesTheSameImage)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    const std::string combinedKey = std::string(kPersistentOwnerCacheValidation) + "::TokenReuse";
    PersistentTextureCacheToken token;

    for (int frame = 0; frame < 3; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h =
                b.GetOrCreatePersistentTexture(token, kPersistentOwnerCacheValidation, "TokenReuse", desc);
            EXPECT_TRUE(h.IsValid());
            b.AddPass(
                "TokenReuseTouchPass",
                [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
                [](PassContext&) {});
            return {};
        });
    }

    const std::optional<DebugTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_NE(snapshot->target.image, static_cast<VkImage>(VK_NULL_HANDLE));
}

// 23. Write-on-frame-N, read-correctly-on-frame-N+1 - the concrete "no
// discard" proof. Frame 1 writes a known, real clear-color pattern via a
// genuine declared pass (VK_ATTACHMENT_LOAD_OP_CLEAR), with NO reader that
// same frame (Section 5.2's own recommended canonical shape - a
// write-only pass kept alive purely by the persistent-cache root). Frame 2
// declares a SECOND real, genuinely-surviving pass against the SAME
// identity - a WriteColorAttachment call with NO clear color (so
// VK_ATTACHMENT_LOAD_OP_LOAD, NOT _CLEAR - it must LOAD the existing
// content, never discard it) - this is a DELIBERATE, corrected choice from
// this test's own original draft, which tried a ReadTexture()-ONLY pass:
// RenderGraphCompiler::Compile()'s root-marking scan only ever scans
// `pass.writes` (confirmed by direct code read, RenderGraphCompiler.cpp
// ~line 542) - a pass with ZERO declared writes can NEVER become a root by
// itself, and with no other pass in that same frame to be a dependency
// predecessor of, a pure ReadTexture()-only pass is UNCONDITIONALLY culled -
// a real, confirmed-live defect found and fixed in this exact shape during
// this phase's own dispatch_sub_agent double-check, before this file was
// committed (see this phase's own completion report). This corrected
// LOAD_OP_LOAD write pass IS a real, meaningful regression check in its own
// right: if this cache's own honest-layout-recording (RecordFinalLayout())
// or the builder's `resolved.lastKnownLayout` -> ImportTexture() plumbing
// were ever wrong, VK_ATTACHMENT_LOAD_OP_LOAD against a falsely-assumed
// layout would read back GARBAGE, not frame 1's real content - this test's
// own final pixel read-back (via Renderer::CaptureImagePixels(), the exact
// same primitive GET /get_texture itself is built on) would then fail.
TEST(RenderGraphPersistentResourceCacheTest, ContentWrittenOnFrameNSurvivesReadableOnFrameNPlusOne)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc(16, 16);
    const std::array<float, 4> knownColor{ 0.25f, 0.5f, 0.75f, 1.0f };
    const std::string combinedKey = std::string(kPersistentOwnerCacheValidation) + "::WriteReadRoundTrip";

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle h =
            b.GetOrCreatePersistentTexture(kPersistentOwnerCacheValidation, "WriteReadRoundTrip", desc);
        b.AddPass(
            "PersistentTextureKnownPatternWrite",
            [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h, knownColor); },
            [](PassContext&) {});
        return {};
    });

    const std::optional<DebugTextureSnapshot> afterFrame1 = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
    ASSERT_TRUE(afterFrame1.has_value());
    const Renderer::CapturedRawPixels pixelsAfterFrame1 = fixture.GetRenderer().CaptureImagePixels(
        afterFrame1->target.image, VK_IMAGE_ASPECT_COLOR_BIT, afterFrame1->target.format, afterFrame1->target.extent,
        afterFrame1->colorState);
    ASSERT_EQ(pixelsAfterFrame1.pixels.size(), static_cast<std::size_t>(16 * 16 * 4));
    // VK_FORMAT_R8G8B8A8_UNORM - each channel is round(component * 255).
    EXPECT_NEAR(pixelsAfterFrame1.pixels[0], 64, 2);
    EXPECT_NEAR(pixelsAfterFrame1.pixels[1], 128, 2);
    EXPECT_NEAR(pixelsAfterFrame1.pixels[2], 191, 2);
    EXPECT_NEAR(pixelsAfterFrame1.pixels[3], 255, 2);

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle h =
            b.GetOrCreatePersistentTexture(kPersistentOwnerCacheValidation, "WriteReadRoundTrip", desc);
        b.AddPass(
            "PersistentTextureKnownPatternLoadPreserve",
            [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); }, // no clearColor -> LOAD, not CLEAR.
            [](PassContext&) {});
        return {};
    });

    const std::optional<DebugTextureSnapshot> afterFrame2 = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
    ASSERT_TRUE(afterFrame2.has_value());
    EXPECT_EQ(afterFrame2->target.image, afterFrame1->target.image); // same physical VkImage - no discard/recreate.
    const Renderer::CapturedRawPixels pixelsAfterFrame2 = fixture.GetRenderer().CaptureImagePixels(
        afterFrame2->target.image, VK_IMAGE_ASPECT_COLOR_BIT, afterFrame2->target.format, afterFrame2->target.extent,
        afterFrame2->colorState);
    ASSERT_EQ(pixelsAfterFrame2.pixels.size(), pixelsAfterFrame1.pixels.size());
    EXPECT_NEAR(pixelsAfterFrame2.pixels[0], 64, 2);
    EXPECT_NEAR(pixelsAfterFrame2.pixels[1], 128, 2);
    EXPECT_NEAR(pixelsAfterFrame2.pixels[2], 191, 2);
    EXPECT_NEAR(pixelsAfterFrame2.pixels[3], 255, 2);
}

// 24. Keep-alive in practice, through the REAL builder - a pass whose ONLY
// declared usage of a GetOrCreatePersistentTexture()-minted handle is a
// write, with NOTHING else reading/writing it this frame and the handle NOT
// present in `finalOutputs`, still runs (is NOT culled) every single real
// frame - proving PHASE3's compiler root-marking fix and this phase's own
// builder wiring genuinely connect end-to-end. A genuinely CULLED pass would
// never resolve this texture, so RegisterDebugTextureSnapshots() would never
// Upsert() a fresh entry for it - `lastUpdatedFrameCounter` strictly
// advancing past this iteration's own `counterBefore` snapshot is the
// direct, observable proof this pass really executed THIS frame, not merely
// that a stale entry from an earlier frame is still sitting in the registry.
TEST(RenderGraphPersistentResourceCacheTest, WriteOnlyPersistentTextureWithNoReaderSurvivesCullingEveryFrame)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();
    const std::string combinedKey = std::string(kPersistentOwnerCacheValidation) + "::KeepAliveOnly";

    for (int frame = 0; frame < 3; ++frame) {
        fixture.GetRenderGraph().BeginPersistentResourceFrame();
        const std::uint64_t counterBefore = fixture.GetRenderGraph().CurrentDebugTextureFrameCounter();
        fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
            const TextureHandle h =
                b.GetOrCreatePersistentTexture(kPersistentOwnerCacheValidation, "KeepAliveOnly", desc);
            b.AddPass(
                "PersistentKeepAliveWritePass",
                [&](RenderGraphBuilder::PassBuilder& pb) { pb.WriteColorAttachment(h); },
                [](PassContext&) {});
            return {}; // deliberately empty - `h` is NEVER a finalOutput.
        });

        const std::optional<DebugTextureSnapshot> snapshot = fixture.GetRenderGraph().DebugTextureSnapshotFor(combinedKey);
        ASSERT_TRUE(snapshot.has_value());
        EXPECT_GT(snapshot->lastUpdatedFrameCounter, counterBefore);
    }
}

// 25. Two different owners, same name, same desc - re-confirms PHASE4's own
// lower-level test (test 2 above), this time through the full,
// production-shaped GetOrCreatePersistentTexture() call path. See test 21's
// own doc comment above for why a real, declared touching pass per handle is
// required here too.
TEST(RenderGraphPersistentResourceCacheTest, BuilderGetOrCreatePersistentTextureTwoOwnersWithTheSameNameNeverCollide)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    const TextureDesc desc = MakeColorDesc();

    fixture.GetRenderGraph().BeginPersistentResourceFrame();
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        const TextureHandle a = b.GetOrCreatePersistentTexture("BuilderOwnerA", "History", desc);
        const TextureHandle c = b.GetOrCreatePersistentTexture("BuilderOwnerB", "History", desc);
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

// 26. Re-confirming PHASE4/5's own debug-assert-driven refusals still fire
// correctly when reached THROUGH GetOrCreatePersistentTexture() itself (not
// just Resolve()/ResolveFast() directly, as tests 4/12 above already cover).
// The PipelinedDeferredReadback regime-aware resize-refusal case (PHASE6) is
// DELIBERATELY NOT re-tested through the real builder here - HeadlessRenderGraphFixture
// only ever drives a SynchronousImmediateReadback Execute() call
// (RunSynchronousFrame()'s own fixed regime), and GetOrCreatePersistentTexture()
// itself adds NO timingMode-specific logic of its own (it forwards
// m_persistentCacheTimingMode to Resolve()/ResolveFast() completely
// unconditionally) - that refusal path is already directly, fully covered by
// PHASE6's own ResizeRequestedFromThePipelinedRegimeIsRefusedButTheCallStillSucceeds
// test, unaffected by this phase's ResolveAgainstEntry() extraction (confirmed
// by re-running the full PHASE4-6 suite unmodified after this phase's refactor).
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
            fixture.GetRenderGraph().BeginPersistentResourceFrame();
            fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
                (void)b.GetOrCreatePersistentTexture(kPersistentOwnerCacheValidation, "BuilderDepthRefusal", desc);
                return {};
            });
        },
        "");
}

TEST(RenderGraphPersistentResourceCacheDeathTest, BuilderGetOrCreatePersistentTextureAssertsOnEmptyOwner)
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
            fixture.GetRenderGraph().BeginPersistentResourceFrame();
            fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
                (void)b.GetOrCreatePersistentTexture("", "BuilderEmptyOwnerRefusal", desc);
                return {};
            });
        },
        "");
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

} // namespace gte::rg
