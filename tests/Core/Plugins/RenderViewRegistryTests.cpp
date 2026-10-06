// Tier-2 (real, headless GPU) tests for gte::RenderViewRegistry.
//
// Every refusal path in CreateOrGetView() is also guarded by a live
// assert() in this project's own Debug ctest build (NDEBUG is never
// defined), so each refusal is exercised as its own *DeathTest case
// instead of a plain EXPECT_EQ, which would abort the whole test process.

#include "Core/Plugins/RenderViewRegistry.h"
#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "Core/LogSink.h"
#include "Editor/Logger.h" // LoggerLogSink::Instance() - restores the real sink afterward.
#include <gtest/gtest.h>

#include <thread>

namespace gte {
namespace {

RenderViewDesc MakeColorDesc(std::uint32_t w = 64, std::uint32_t h = 64)
{
    RenderViewDesc d; d.width = w; d.height = h; d.hasColor = true; d.hasDepth = true;
    d.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    return d;
}

RenderViewDesc MakeDepthOnlyDesc(std::uint32_t w = 64, std::uint32_t h = 64)
{
    RenderViewDesc d; d.width = w; d.height = h; d.hasColor = false; d.hasDepth = true;
    return d;
}

} // namespace

// 1. Idempotent by name, distinct across names.
TEST(RenderViewRegistryTest, SameNameReturnsSameIdDifferentNameReturnsDifferentId)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    const rg::RenderViewId first = registry.CreateOrGetView("X", MakeColorDesc());
    const rg::RenderViewId second = registry.CreateOrGetView("X", MakeColorDesc());
    const rg::RenderViewId other = registry.CreateOrGetView("Y", MakeColorDesc());
    EXPECT_EQ(first, second);
    EXPECT_NE(first, other);
}

// 2. Mismatched desc on an existing name - refused, logged exactly once ever.
TEST(RenderViewRegistryTest, MismatchedDescOnExistingNameIsRefusedAndLoggedExactlyOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());

    class RecordingLogSink : public ILogSink {
    public:
        void Log(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false) override
        {
            (void)isBlocking;
            if (category == "RenderViewRegistry" && level == LogLevel::Error
                && message.find("DIFFERENT desc") != std::string_view::npos) {
                ++errorCount;
            }
        }
        int errorCount = 0;
    } sink;
    InstallLogSink(&sink);

    const rg::RenderViewId first = registry.CreateOrGetView("X", MakeColorDesc(64, 64));
    const rg::RenderViewId second = registry.CreateOrGetView("X", MakeColorDesc(128, 128));
    const rg::RenderViewId third = registry.CreateOrGetView("X", MakeColorDesc(256, 256));

    InstallLogSink(&LoggerLogSink::Instance());

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, third);
    EXPECT_EQ(sink.errorCount, 1);
    RenderTexture* target = registry.FindViewTarget(first);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Extent().width, 64u); // unchanged - still the FIRST request's size.
}

// 3. colorFormat ignored for a depth-only view's comparison.
TEST(RenderViewRegistryTest, ColorFormatIsIgnoredWhenHasColorIsFalse)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    RenderViewDesc a = MakeDepthOnlyDesc();
    a.colorFormat = VK_FORMAT_R8G8B8A8_UNORM; // meaningless, since hasColor == false.
    RenderViewDesc b = MakeDepthOnlyDesc();
    b.colorFormat = VK_FORMAT_R16G16B16A16_SFLOAT; // different, still meaningless.
    EXPECT_TRUE(DescsMatchForSameName(a, b));
    const rg::RenderViewId first = registry.CreateOrGetView("DepthOnlyX", a);
    const rg::RenderViewId second = registry.CreateOrGetView("DepthOnlyX", b);
    EXPECT_EQ(first, second);
}

// 3b. allowDepthSampledAccess == true actually reaches a real VkSampler.
TEST(RenderViewRegistryTest, AllowDepthSampledAccessProducesARealDepthSampler)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    RenderViewDesc desc = MakeDepthOnlyDesc();
    desc.allowDepthSampledAccess = true;
    const rg::RenderViewId view = registry.CreateOrGetView("SampledDepthView", desc);
    RenderTexture* target = registry.FindViewTarget(view);
    ASSERT_NE(target, nullptr);
    EXPECT_NE(target->DepthSampler(), static_cast<VkSampler>(VK_NULL_HANDLE));
}

// 3c. allowDepthSampledAccess differing on a re-request is a mismatched desc,
// same as any other field - refused, logged exactly once.
TEST(RenderViewRegistryTest, MismatchedAllowDepthSampledAccessIsRefusedAndLoggedExactlyOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());

    class RecordingLogSink : public ILogSink {
    public:
        void Log(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false) override
        {
            (void)isBlocking;
            if (category == "RenderViewRegistry" && level == LogLevel::Error
                && message.find("DIFFERENT desc") != std::string_view::npos) {
                ++errorCount;
            }
        }
        int errorCount = 0;
    } sink;
    InstallLogSink(&sink);

    RenderViewDesc withSampling = MakeDepthOnlyDesc();
    withSampling.allowDepthSampledAccess = true;
    RenderViewDesc withoutSampling = MakeDepthOnlyDesc();
    withoutSampling.allowDepthSampledAccess = false;

    const rg::RenderViewId first = registry.CreateOrGetView("SampledDepthMismatch", withSampling);
    const rg::RenderViewId second = registry.CreateOrGetView("SampledDepthMismatch", withoutSampling);

    InstallLogSink(&LoggerLogSink::Instance());

    EXPECT_EQ(first, second);
    EXPECT_EQ(sink.errorCount, 1);
    RenderTexture* target = registry.FindViewTarget(first);
    ASSERT_NE(target, nullptr);
    EXPECT_NE(target->DepthSampler(), static_cast<VkSampler>(VK_NULL_HANDLE)); // unchanged - still the FIRST request.
}

// 4. Reserved names / null / empty name / "neither color nor depth" /
// allowDepthSampledAccess-without-depth are all refused via assert()-guarded
// code paths in CreateOrGetView(). This project's own ctest Debug build
// never defines NDEBUG, so each is its own death test rather than a plain
// EXPECT_EQ, which would abort the whole test process instead of exercising
// the GTE_LOG_ERROR()-and-return-Shared() refusal path.
#ifndef NDEBUG

TEST(RenderViewRegistryDeathTest, ReservedNameGameAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            (void)registry.CreateOrGetView("Game", MakeColorDesc());
        },
        "");
}

TEST(RenderViewRegistryDeathTest, ReservedNameSceneAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            (void)registry.CreateOrGetView("Scene", MakeColorDesc());
        },
        "");
}

TEST(RenderViewRegistryDeathTest, NullNameAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            (void)registry.CreateOrGetView(nullptr, MakeColorDesc());
        },
        "");
}

TEST(RenderViewRegistryDeathTest, EmptyNameAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            (void)registry.CreateOrGetView("", MakeColorDesc());
        },
        "");
}

TEST(RenderViewRegistryDeathTest, NeitherColorNorDepthAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            RenderViewDesc desc = MakeColorDesc();
            desc.hasColor = false;
            desc.hasDepth = false;
            (void)registry.CreateOrGetView("NeitherHalf", desc);
        },
        "");
}

TEST(RenderViewRegistryDeathTest, AllowDepthSampledAccessWithoutDepthAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            RenderViewDesc desc = MakeColorDesc();
            desc.hasDepth = false;
            desc.allowDepthSampledAccess = true;
            (void)registry.CreateOrGetView("SampledButNoDepth", desc);
        },
        "");
}

// Main-thread guard: calling CreateOrGetView() from a worker thread aborts.
TEST(RenderViewRegistryDeathTest, CreateOrGetViewFromWorkerThreadAsserts)
{
    {
        HeadlessRenderGraphFixture probe;
        if (!probe.IsUsable()) { GTEST_SKIP() << probe.SkipReason(); }
    }
    EXPECT_DEATH(
        {
            HeadlessRenderGraphFixture fixture;
            RenderViewRegistry registry(fixture.GetRenderer());
            std::thread worker([&registry]() {
                (void)registry.CreateOrGetView("FromWorkerThread", MakeColorDesc());
            });
            worker.join();
        },
        "");
}

#endif

// 4b. FindViewTarget() against the sentinel Shared() id, on a totally
// fresh, empty registry - never calls CreateOrGetView() at all, so no
// assert is ever reachable here; safe as an ordinary (non-death) test in
// every build configuration, unlike every case directly above.
TEST(RenderViewRegistryTest, SharedSentinelNeverResolvesToATarget)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    EXPECT_EQ(registry.FindViewTarget(rg::RenderViewId::Shared()), nullptr);
}

// 5. FindViewTarget() nullptr-for-unknown, non-null-for-known.
TEST(RenderViewRegistryTest, FindViewTargetIsDefensive)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    EXPECT_EQ(registry.FindViewTarget(rg::RenderViewId::Named("NeverCreated")), nullptr);
    const rg::RenderViewId created = registry.CreateOrGetView("KnownView", MakeColorDesc());
    EXPECT_NE(registry.FindViewTarget(created), nullptr);
}

// 6. Colorless view -> exactly one GPU allocation (depth only).
TEST(RenderViewRegistryTest, DepthOnlyViewAllocatesExactlyOneGpuResource)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    const GpuMemoryTracker::Totals before = fixture.GetRenderer().GetMemoryTotals();
    const rg::RenderViewId view = registry.CreateOrGetView("ShadowTest", MakeDepthOnlyDesc());
    const GpuMemoryTracker::Totals after = fixture.GetRenderer().GetMemoryTotals();
    EXPECT_EQ(after.textureCount, before.textureCount + 1);
    RenderTexture* target = registry.FindViewTarget(view);
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Image(), static_cast<VkImage>(VK_NULL_HANDLE));
}

// 7. Exception safety - a failed construction never permanently wedges a name.
TEST(RenderViewRegistryTest, FailedCreateOrGetViewLeavesTheNameRetryableAfterward)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) { GTEST_SKIP() << fixture.SkipReason(); }
    RenderViewRegistry registry(fixture.GetRenderer());
    bool threw = false;
    try {
        (void)registry.CreateOrGetView("HugeView", MakeColorDesc(100000, 100000));
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        GTEST_SKIP() << "This machine's driver/GPU did not reject a 100000x100000 image as expected - see "
                        "RenderGraphPersistentResourceCacheTests.cpp's own identical, honestly-documented "
                        "machine-dependent limitation.";
    }
    const rg::RenderViewId retry = registry.CreateOrGetView("HugeView", MakeColorDesc());
    EXPECT_NE(registry.FindViewTarget(retry), nullptr);
}

} // namespace gte
