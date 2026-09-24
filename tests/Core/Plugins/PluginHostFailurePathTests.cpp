// tests/Core/Plugins/PluginHostFailurePathTests.cpp
//
// editor-core-separation-4 campaign, PHASE8
// (PHASE8_PLUGINHOST_FAILURE_PATH_REGRESSION_TESTS_AND_CAMPAIGN_CLOSEOUT.md)
// - Issue #8 (LOW): real, automated GoogleTest coverage for all 4 of
// PluginHost's own documented failure/skip paths (missing export,
// fingerprint mismatch, decline-to-load, reverse-order destroy), using
// deliberately-broken fixture .dll's under tests/Fixtures/FakePlugins/ -
// never the real, production demo plugins.

#include "Core/Plugins/PluginHost.h"
#include "Core/Logging.h"
#include "Editor/Logger.h" // Logger::Query()/LatestEntryId() - direct log inspection, so each fixture's failure is confirmed via ITS OWN distinct mechanism, not just an aggregate module count.
#include "generated/FakePluginFixturesDirGenerated.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace gte {
namespace {

// Returns true if ANY log entry recorded since `sinceId`, category
// "PluginHost", contains `keyword` (case-sensitive substring match) in its
// message. Used below to prove each of the 3 "unhappy path" fixtures failed
// via ITS OWN intended, distinct mechanism - not merely that the aggregate
// LoadedModuleCount() ended up zero, which would equally pass even if, say,
// fake_plugin_bad_fingerprint accidentally tripped the "missing export"
// path instead of the "fingerprint mismatch" path it's meant to prove.
bool AnyLogMessageContains(std::uint64_t sinceId, const std::string& keyword)
{
    LogQueryFilter filter;
    filter.sinceId = sinceId;
    filter.category = "PluginHost";
    for (const LogEntry& entry : Logger::Query(filter)) {
        if (entry.message.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TEST(PluginHostFailurePathTest, LoadPlugins_UnhappyPathFolder_LoadsExactlyZeroModulesAndEachFixtureFailsForItsOwnDistinctReason)
{
    // fake_plugin_missing_export.dll / fake_plugin_bad_fingerprint.dll /
    // fake_plugin_declines_to_load.dll each fail a DIFFERENT one of
    // PluginHost's own 3 load-time checks (missing export / fingerprint
    // mismatch / decline-to-load) - none of the three may ever produce a
    // loaded module, and none of them may ever crash the host.
    const std::uint64_t baselineId = Logger::LatestEntryId();

    PluginHost host;
    host.LoadPlugins(test::kFakePluginUnhappyPathFixturesDir);
    EXPECT_EQ(host.LoadedModuleCount(), 0u);

    // Each fixture's own real, current .dll filename (fake_plugin_missing_export,
    // etc. - PREFIX "" + the CMake target name) is part of PluginHost's own
    // logged path prefix on every one of its 3 checked messages, so matching
    // on it ties each log line back to the specific fixture that produced it.
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "fake_plugin_missing_export"))
        << "expected a log line naming fake_plugin_missing_export.dll";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "is missing export"))
        << "expected the missing-export skip message";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "fake_plugin_bad_fingerprint"))
        << "expected a log line naming fake_plugin_bad_fingerprint.dll";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "ABI fingerprint mismatch"))
        << "expected the fingerprint-mismatch skip message";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "fake_plugin_declines_to_load"))
        << "expected a log line naming fake_plugin_declines_to_load.dll";
    EXPECT_TRUE(AnyLogMessageContains(baselineId, "declined to load"))
        << "expected the decline-to-load message";
}

TEST(PluginHostFailurePathTest, LoadPlugins_DestroyOrderFolder_LoadsExactlyTwoModules)
{
    PluginHost host;
    host.LoadPlugins(test::kFakePluginDestroyOrderFixturesDir);
    EXPECT_EQ(host.LoadedModuleCount(), 2u);
}

TEST(PluginHostFailurePathTest, Destructor_DestroysLoadedModulesInExactReverseOfTheirCreateOrder)
{
    const std::filesystem::path markerPath =
        std::filesystem::temp_directory_path() / "gte_plugin_destroy_order_test_marker.txt";
    std::filesystem::remove(markerPath); // start from a clean slate, in case a prior run left it behind

    // _putenv() (single "NAME=value" string form), NOT _putenv_s() - this
    // exact toolchain already has a real, mechanically-confirmed precedent
    // for this exact call on GTEST_OS_WINDOWS: third_party/googletest's own
    // internal SetEnv() helper uses
    // `_putenv((Message() << name << "=" << value).GetString().c_str())`
    // on GTEST_OS_WINDOWS specifically.
    const std::string setMarkerEnv = std::string("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE=") + markerPath.string();
    _putenv(setMarkerEnv.c_str());

    {
        PluginHost host; // scoped - its destructor runs at the closing brace below
        host.LoadPlugins(test::kFakePluginDestroyOrderFixturesDir);
        ASSERT_EQ(host.LoadedModuleCount(), 2u);
    } // ~PluginHost() runs here - both fixtures' GTE_DestroyPluginModule() fire, in reverse load order

    _putenv("GTE_PLUGIN_DESTROY_ORDER_MARKER_FILE="); // unset (empty value) for any later test in this same process

    std::ifstream markerFile(markerPath);
    ASSERT_TRUE(markerFile.is_open());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(markerFile, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    markerFile.close();
    std::filesystem::remove(markerPath); // clean up after ourselves

    // Extract CREATE order and DESTROY order independently from the marker
    // file, rather than assuming std::filesystem::directory_iterator's own
    // (unspecified by the standard) enumeration order - this test only
    // asserts the real, load-bearing invariant PluginHost promises: destroy
    // order is the EXACT REVERSE of create order, whatever that real create
    // order turned out to be.
    std::vector<std::string> createOrder;
    std::vector<std::string> destroyOrder;
    for (const std::string& entry : lines) {
        if (entry.rfind("CREATE:", 0) == 0) {
            createOrder.push_back(entry.substr(7));
        } else if (entry.rfind("DESTROY:", 0) == 0) {
            destroyOrder.push_back(entry.substr(8));
        }
    }

    ASSERT_EQ(createOrder.size(), 2u);
    ASSERT_EQ(destroyOrder.size(), 2u);
    std::vector<std::string> expectedDestroyOrder(createOrder.rbegin(), createOrder.rend());
    EXPECT_EQ(destroyOrder, expectedDestroyOrder);
}

} // namespace
} // namespace gte
