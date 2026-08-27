// Tier 1: verifies SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency()
// (the ONE clock this engine's Profiling module is built on - see
// AGENTS.md, "Profiling") are safe to call CONCURRENTLY, from several
// threads at once, with no external synchronization of any kind. This is
// the concrete verification item
// JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md (Job System
// Phase 4, Step 3.2) calls for before Phase 5's JobScopeTimer may rely on
// calling both of these functions from an arbitrary worker thread while the
// main thread might simultaneously be doing the exact same thing for its
// own ScopeTimer scopes.
//
// No SDL_Init()/video subsystem is needed here - SDL_GetPerformanceCounter()/
// SDL_GetPerformanceFrequency() are plain, stateless queries against a
// platform-level monotonic counter, independent of SDL's subsystems (the
// same "SDL types/functions that don't need SDL_Init()" pattern already
// established by tests/Memory/SdlMemoryTrackerTests.cpp and
// tests/Application/EventTranslatorTests.cpp).
//
// Both tests below deliberately use a shared start barrier (every worker
// thread blocks until every OTHER thread has also registered as ready, then
// all are released at once) rather than just spawning threads and letting
// them run "eventually" - per
// JOBSYSTEM_PHASE5_PROFILER_INTEGRATION_WORKER_TIMELINE_v2.md's own Step
// 3.5 guidance, this is what actually exercises "many threads calling this
// AT THE SAME INSTANT", rather than a weaker test that could pass even if
// concurrent calls were subtly unsafe but simply never happened to overlap
// during a given run.

#include <SDL3/SDL_timer.h>

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace gte {
namespace {

// Small helper shared by both tests below: blocks the calling thread until
// every one of `threadCount` participants (including this one) has called
// Arrive(), then releases all of them at (as close to) the same instant.
class StartBarrier {
public:
    explicit StartBarrier(int threadCount)
        : m_threadCount(threadCount)
    {
    }

    void Arrive()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        ++m_readyCount;
        if (m_readyCount == m_threadCount) {
            m_go = true;
            m_condition.notify_all();
            return;
        }
        m_condition.wait(lock, [this]() { return m_go; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    int m_threadCount;
    int m_readyCount = 0;
    bool m_go = false;
};

TEST(JobSystemSdlClockThreadSafetyTests, FrequencyIsConsistentAcrossConcurrentThreads)
{
    constexpr int kThreadCount = 8;
    std::vector<std::uint64_t> frequencies(static_cast<std::size_t>(kThreadCount), 0);
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kThreadCount));

    StartBarrier barrier(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&barrier, &frequencies, i]() {
            barrier.Arrive();
            frequencies[static_cast<std::size_t>(i)] = SDL_GetPerformanceFrequency();
        });
    }

    for (std::thread& t : threads) {
        t.join();
    }

    // SDL_GetPerformanceFrequency() is documented as a fixed value for the
    // life of the process - every thread must observe the exact same,
    // non-zero value, even when every one of them queries it at
    // (approximately) the same instant.
    ASSERT_NE(frequencies[0], static_cast<std::uint64_t>(0));
    for (int i = 1; i < kThreadCount; ++i) {
        EXPECT_EQ(frequencies[static_cast<std::size_t>(i)], frequencies[0]);
    }
}

TEST(JobSystemSdlClockThreadSafetyTests, CounterIsMonotonicPerThreadUnderConcurrentCalls)
{
    constexpr int kThreadCount = 8;
    constexpr int kSamplesPerThread = 2000;

    std::vector<std::vector<std::uint64_t>> samples(static_cast<std::size_t>(kThreadCount));
    for (auto& perThread : samples) {
        perThread.resize(static_cast<std::size_t>(kSamplesPerThread));
    }

    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(kThreadCount));

    StartBarrier barrier(kThreadCount);

    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&barrier, &samples, i]() {
            barrier.Arrive();
            std::vector<std::uint64_t>& mine = samples[static_cast<std::size_t>(i)];
            for (std::uint64_t& sample : mine) {
                sample = SDL_GetPerformanceCounter();
            }
        });
    }

    for (std::thread& t : threads) {
        t.join();
    }

    // Every single thread's OWN sequence of reads must be non-decreasing -
    // a torn/corrupted read from an unsafe concurrent implementation would
    // show up as a nonsensical backward jump within one thread's own
    // sequence. This is the actual, concrete claim this phase's audit
    // needed verified: calling SDL_GetPerformanceCounter() from many
    // threads at the same time never corrupts any individual thread's own
    // results.
    for (int i = 0; i < kThreadCount; ++i) {
        const std::vector<std::uint64_t>& mine = samples[static_cast<std::size_t>(i)];
        for (std::size_t s = 1; s < mine.size(); ++s) {
            ASSERT_GE(mine[s], mine[s - 1]) << "thread=" << i << " sample=" << s;
        }
    }
}

} // namespace
} // namespace gte
