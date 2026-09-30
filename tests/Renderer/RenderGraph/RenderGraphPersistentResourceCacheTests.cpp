// editor-core-separation-27 campaign, PHASE4
// (task_manager/editor-core-separation-27/
// PHASE4_HEADLESS_TEST_FIXTURE_AND_CACHE_CONSTRUCTION.md) - Tier-2 (real,
// headless GPU) tests for RenderGraphPersistentResourceCache's own
// construction/ownership/exception-safety core (source document Section 6,
// 6.1, 6.2, 7). GTEST_SKIP()-guarded exactly like every other
// HeadlessSurfaceProvider consumer in this codebase - see
// tests/Fakes/HeadlessRenderGraphFixture.h's own top-of-file comment.
//
// Scope: THIS phase's own construction/collision-safety/exception-safety/
// color-only acceptance criteria only. Age-tracking, the same-frame
// double-request guard, eviction, resize, and honest-layout-recording are
// all LATER phases of this same campaign (PHASE5/PHASE6/PHASE8), extending
// this SAME class - none of that is attempted here.

#include "Renderer/RenderGraph/RenderGraphPersistentResourceCache.h"
#include "Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

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
            resolved = cache.Resolve(kPersistentOwnerCacheValidation, "BasicReuse", desc);
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
        resolvedA = cache.Resolve("OwnerA", "History", desc);
        resolvedB = cache.Resolve("OwnerB", "History", desc);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "ExceptionSafety", hugeDesc);
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
        retryResolved = cache.Resolve(kPersistentOwnerCacheValidation, "ExceptionSafety", saneDesc);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "DepthRefusal", desc);
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
            (void)cache.Resolve(nullptr, "NullOwnerRefusal", desc);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, nullptr, desc);
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
            (void)cache.Resolve("", "EmptyOwnerRefusal", desc);
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
            (void)cache.Resolve(kPersistentOwnerCacheValidation, "", desc);
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
        resolved = cache.Resolve(kPersistentOwnerCacheValidation, "NoDepthCheck", desc);
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
                cache.Resolve(kPersistentOwnerCacheValidation, name.c_str(), desc);
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
                cache.Resolve(kPersistentOwnerCacheValidation, entry.name.c_str(), desc);
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

} // namespace gte::rg
