// Tier 1: GTE_PROFILE_FUNCTION names, checked against what the real compiler prints.
#include "Profiling/FrameProfiler.h"
#include "Profiling/NameTable.h"
#include "Profiling/ScopeSource.h"
#include "Profiling/ScopeTimer.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#if GTE_ENABLE_PROFILER

namespace macro_probe {

struct Widget {
    void Update() { GTE_PROFILE_FUNCTION(); }
};

template <class T>
struct Box {
    void Run(T) { GTE_PROFILE_FUNCTION(); }
};

void FreeFunction()
{
    GTE_PROFILE_FUNCTION();
}

void RunLambda()
{
    auto lambda = [](int) { GTE_PROFILE_FUNCTION(); };
    lambda(1);
}

} // namespace macro_probe

namespace {

void Helper()
{
    GTE_PROFILE_FUNCTION();
}

} // namespace

namespace gte::Profiling {
namespace {

TEST(ScopeFunctionMacroTest, NamesComeFromTheEnclosingFunction)
{
    FrameProfiler& profiler = FrameProfiler::Instance();
    profiler.ResetForTesting();
    profiler.SetCaptureEnabled(true);

    profiler.BeginFrame();
    macro_probe::Widget{}.Update();
    macro_probe::Box<int>{}.Run(1);
    macro_probe::FreeFunction();
    macro_probe::RunLambda();
    Helper();
    profiler.EndFrame();

    std::vector<std::string> names;
    for (const ScopeEvent& e : profiler.LastCompletedFrame().scopes.Events()) {
        names.emplace_back(ResolveName(e.name));
    }
    const std::vector<std::string> expected{
        "macro_probe::Widget::Update",
        "macro_probe::Box<T>::Run",
        "macro_probe::FreeFunction",
        "macro_probe::RunLambda::<lambda>",
        "{anonymous}::Helper",
    };
    EXPECT_EQ(names, expected);

    const NameId id = profiler.LastCompletedFrame().scopes.Events()[0].name;
    EXPECT_STREQ(ResolveName(SourceOf(id).fileName), "ScopeFunctionMacroTests.cpp");
    EXPECT_GT(SourceOf(id).line, 0u);

    profiler.ResetForTesting();
}

} // namespace
} // namespace gte::Profiling

#endif // GTE_ENABLE_PROFILER
