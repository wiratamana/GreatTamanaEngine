// Tier 1: the profiling clock is safe to read from many threads at once.
#include "Profiling/ProfilingClock.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace gte {
namespace {

// Holds every thread until all have arrived, then releases them together.
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

TEST(JobSystemSdlClockThreadSafetyTests, TicksAreNanoseconds)
{
    EXPECT_DOUBLE_EQ(Profiling::TicksToMilliseconds(1'000'000), 1.0);
    EXPECT_DOUBLE_EQ(Profiling::TicksToMilliseconds(0), 0.0);
    EXPECT_DOUBLE_EQ(Profiling::kTicksPerMillisecond, 1.0e6);
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
            for (std::uint64_t& sample : samples[static_cast<std::size_t>(i)]) {
                sample = Profiling::GetProfilingPerformanceCounter();
            }
        });
    }

    for (std::thread& t : threads) {
        t.join();
    }

    // A torn read would show up as a backward jump inside one thread's own sequence.
    for (int i = 0; i < kThreadCount; ++i) {
        const std::vector<std::uint64_t>& mine = samples[static_cast<std::size_t>(i)];
        for (std::size_t s = 1; s < mine.size(); ++s) {
            ASSERT_GE(mine[s], mine[s - 1]) << "thread=" << i << " sample=" << s;
        }
    }
}

} // namespace
} // namespace gte
