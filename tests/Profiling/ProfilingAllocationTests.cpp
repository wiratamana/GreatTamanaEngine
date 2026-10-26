#include "Profiling/FrameGate.h"
#include "Profiling/ManualClock.h"
#include "Profiling/NameTable.h"
#include "Profiling/ScopeLog.h"
#include "Profiling/ScopeTree.h"
#include "Profiling/WorkerRing.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>

namespace {

// Heap allocations made by the calling thread. Only deltas are read.
thread_local std::uint64_t t_allocations = 0;

} // namespace

// Counting replacement for the global allocator. It forwards to malloc and free.
void* operator new(std::size_t size)
{
    ++t_allocations;
    if (void* p = std::malloc(size != 0 ? size : 1)) {
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

namespace gte::Profiling {
namespace {

TEST(ProfilingAllocationTest, RecordingScopesAndMarkersNeverAllocates)
{
    FrameGate::MarkMainThread();
    auto log = std::make_unique<ScopeLog>();
    const NameId id = InternName("ProfilingAllocationTest.Scope");
    ManualClock clock;

    log->BeginFrame(true);
    const std::uint64_t before = t_allocations;
    for (int i = 0; i < 1500; ++i) {
        const ScopeStart outer = log->Begin(clock.NowTicks());
        const ScopeStart inner = log->Begin(clock.NowTicks());
        clock.AdvanceTicks(3);
        log->End(inner, id, ScopeKind::Work, ViewSlot::Primary, clock.NowTicks());
        log->End(outer, id, ScopeKind::Blocked, ViewSlot::Shared, clock.NowTicks());
    }
    log->Marker(id, ViewSlot::Shared, clock.NowTicks());
    log->EndFrame();
    log->BeginFrame(true); // offered more events than fit: the overflow path must not allocate either
    const std::uint64_t after = t_allocations;

    EXPECT_EQ(after, before);
}

TEST(ProfilingAllocationTest, InterningAKnownNameAndResolvingNeverAllocates)
{
    const NameId id = InternName("ProfilingAllocationTest.Name");
    const char* resolved = nullptr;

    const std::uint64_t before = t_allocations;
    bool stable = true;
    for (int i = 0; i < 1000; ++i) {
        stable = stable && InternName("ProfilingAllocationTest.Name") == id;
        resolved = ResolveName(id);
    }
    const std::uint64_t after = t_allocations;

    EXPECT_EQ(after, before);
    EXPECT_TRUE(stable);
    EXPECT_STREQ(resolved, "ProfilingAllocationTest.Name");
}

TEST(ProfilingAllocationTest, WorkerRingPushAndDrainNeverAllocate)
{
    auto ring = std::make_unique<WorkerRing>();
    auto buffer = std::make_unique<WorkerEventBuffer>();

    const std::uint64_t before = t_allocations;
    for (std::uint64_t i = 0; i < 2000; ++i) { // laps the ring on purpose
        ring->Push(WorkerEvent{ i, i + 1, 2, 1, 0, 0 });
    }
    const WorkerDrainStats stats = ring->Drain(0, ~std::uint64_t{ 0 }, *buffer);
    const std::uint64_t after = t_allocations;

    EXPECT_EQ(after, before);
    EXPECT_GT(stats.drained, 0u);
}

TEST(ProfilingAllocationTest, ComputingScopeTimesNeverAllocates)
{
    std::array<ScopeEvent, 4> events{ {
        { 0, 10, 2, 1, ScopeKind::Work, ViewSlot::Shared },
        { 10, 20, 2, 1, ScopeKind::Work, ViewSlot::Shared },
        { 0, 30, 2, 0, ScopeKind::Work, ViewSlot::Shared },
        { 30, 40, 2, 0, ScopeKind::Work, ViewSlot::Shared },
    } };
    std::array<ScopeTimes, 4> times{};

    const std::uint64_t before = t_allocations;
    const ScopeTreeReport report = ComputeScopeTimes(events, times);
    const std::uint64_t after = t_allocations;

    EXPECT_EQ(after, before);
    EXPECT_EQ(report.childrenExceedParent, 0u);
    EXPECT_EQ(times[2].selfTicks, 10u);
}

} // namespace
} // namespace gte::Profiling
