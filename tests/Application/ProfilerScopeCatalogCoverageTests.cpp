// Tier 1: every hand-written scope name in src has a catalog entry.
#include "Application/ProfilerScopeCatalog.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace gte {
namespace {

using Profiling::ScopeLane;
using Profiling::ScopeOrigin;

struct MacroKind {
    std::string_view token; // includes the opening parenthesis
    ScopeLane lane;
};

constexpr MacroKind kMacros[] = {
    { "GTE_PROFILE_SCOPE(", ScopeLane::Main },
    { "GTE_PROFILE_BLOCKED(", ScopeLane::Main },
    { "GTE_PROFILE_JOB_SCOPE(", ScopeLane::Workers },
};

std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void SkipSpaces(const std::string& text, std::size_t& pos)
{
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r' || text[pos] == '\n')) {
        ++pos;
    }
}

// Collects the plain string literals passed to one macro. Other arguments are skipped.
void CollectNames(const std::string& text, const MacroKind& macro, std::set<std::string>& out)
{
    for (std::size_t at = text.find(macro.token); at != std::string::npos; at = text.find(macro.token, at + 1)) {
        std::size_t pos = at + macro.token.size();
        SkipSpaces(text, pos);
        if (pos >= text.size() || text[pos] != '"') {
            continue;
        }
        const std::size_t end = text.find('"', pos + 1);
        if (end == std::string::npos) {
            continue;
        }
        const std::string name = text.substr(pos + 1, end - pos - 1);
        pos = end + 1;
        SkipSpaces(text, pos);
        if (pos < text.size() && text[pos] == ')' && name.find('\\') == std::string::npos) {
            out.insert(name);
        }
    }
}

TEST(ProfilerScopeCatalogCoverageTest, EveryHandWrittenScopeNameIsDescribed)
{
#ifndef GTE_REPO_SRC_DIR
    GTEST_SKIP() << "GTE_REPO_SRC_DIR is not defined";
#else
    std::set<std::string> missing;
    std::size_t checked = 0;
    for (const auto& item : std::filesystem::recursive_directory_iterator(GTE_REPO_SRC_DIR)) {
        if (!item.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& path = item.path();
        const std::string ext = path.extension().string();
        const std::string file = path.filename().string();
        if ((ext != ".cpp" && ext != ".h") || file == "ScopeTimer.h" || file == "JobScopeTimer.h") {
            continue;
        }
        const std::string text = ReadAll(path);
        for (const MacroKind& macro : kMacros) {
            std::set<std::string> names;
            CollectNames(text, macro, names);
            for (const std::string& name : names) {
                ++checked;
                if (DescribeScope(name, macro.lane, ScopeOrigin::Engine).generic) {
                    missing.insert(name);
                }
            }
        }
    }

    EXPECT_GT(checked, 0u);
    std::string list;
    for (const std::string& name : missing) {
        list += "\n  " + name;
    }
    EXPECT_TRUE(missing.empty()) << "scope names without a catalog entry:" << list;
#endif
}

} // namespace
} // namespace gte
