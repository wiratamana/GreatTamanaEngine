// Tier 1: profiler JSON shapes, the "every number sits in a metric" rule, and the snapshot hand-off.
#include "Application/ProfilerSnapshotBridge.h"
#include "Network/NetworkProfilerRoutes.h"
#include "Profiling/FrameProfiler.h"
#include "Profiling/ProfilerSnapshotBuilder.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace gte::Network {
namespace {

using Profiling::Metric;
using Profiling::ProfilerSnapshot;
using nlohmann::json;

Metric Ms(double value)
{
    return Profiling::PresentMetric(value, Profiling::Unit::Milliseconds);
}

Metric Cnt(double value)
{
    return Profiling::PresentMetric(value, Profiling::Unit::Count);
}

// Every field is populated so the walker sees every kind of value.
ProfilerSnapshot FullSnapshot()
{
    ProfilerSnapshot s;
    s.frameIndex = 42;
    s.hasFrame = true;
    s.snapshotAge = Ms(1.5);
    s.cpuFrame = Ms(16.0);
    s.untracked = Ms(0.5);
    s.blockedGpu = Ms(2.0);
    s.blockedWorkers = Ms(0.1);
    s.blockedOther = Ms(0.0);
    s.gpuSpan = Ms(9.0);
    for (Metric& view : s.gpuView) {
        view = Ms(4.0);
    }
    s.gpuView[static_cast<std::size_t>(Profiling::ViewSlot::Secondary)] =
        Profiling::MissingMetric(Profiling::Unit::Milliseconds, Profiling::SampleStatus::Inactive, Profiling::Reason::NoTarget);
    for (Metric& draws : s.indirectDraws) {
        draws = Cnt(3);
    }
    s.gpuCoverage = { Cnt(10), Cnt(9), Cnt(9), Cnt(1) };
    s.memoryTotal = Profiling::PresentMetric(1024.0 * 1024.0, Profiling::Unit::Bytes);

    for (std::size_t i = 0; i < Profiling::kCheckCount; ++i) {
        s.checks[i] = { static_cast<Profiling::CheckId>(i), Profiling::CheckStatus::Ok, Cnt(0), "check text" };
    }
    s.drops = { Cnt(1), Cnt(2), Cnt(3), Cnt(4), Cnt(5) };
    s.device.deviceLost = false;
    s.device.consecutiveFrameTimeouts = Cnt(1);
    s.device.abandonedObjects = Cnt(2);

    s.liveBlockCount = 1;
    s.liveBlocks[0].threadSlot = 3;
    Profiling::CopySnapshotText(s.liveBlocks[0].site, "Offscreen");
    Profiling::CopySnapshotText(s.liveBlocks[0].detail, "fence");
    s.liveBlocks[0].elapsed = Ms(12.0);

    Profiling::CopySnapshotText(s.verdict, "unavailable");

    s.scopeCount = 2;
    for (std::uint32_t i = 0; i < s.scopeCount; ++i) {
        Profiling::CopySnapshotText(s.scopes[i].name, i == 0 ? "Render.Prepare" : "Present");
        s.scopes[i].total = Ms(3.0);
        s.scopes[i].self = Ms(1.0);
        s.scopes[i].calls = Cnt(1);
    }
    s.scopeRowsDropped = 0;

    s.frameCount = 3;
    for (std::uint32_t i = 0; i < s.frameCount; ++i) {
        s.frames[i].frameIndex = 40 + i;
        s.frames[i].cpu = Ms(16.0);
        s.frames[i].gpuSpan = Ms(9.0);
        s.frames[i].gpuPrimary = Ms(4.0);
        s.frames[i].gpuSecondary = Ms(4.0);
    }

    s.spikesAvailable = true;
    s.spikeCount = 1;
    s.spikes[0].frameIndex = 41;
    s.spikes[0].cpu = Ms(50.0);
    Profiling::CopySnapshotText(s.spikes[0].classification, "unexplained");
    Profiling::CopySnapshotText(s.spikes[0].cause, "Present");
    s.spikePinned = 1;
    s.spikeTotal = 3;
    return s;
}

bool IsMetricObject(const json& value)
{
    if (!value.is_object()) {
        return false;
    }
    for (const std::string_view key : kProfilerMetricKeys) {
        if (!value.contains(std::string(key))) {
            return false;
        }
    }
    return true;
}

bool IsFixedKey(const std::string& key)
{
    for (const std::string_view fixed : kProfilerFixedNumberKeys) {
        if (fixed == key) {
            return true;
        }
    }
    return false;
}

// Collects the path of every number that sits outside a metric object and is not a fixed key.
void FindStrayNumbers(const json& value, const std::string& path, std::vector<std::string>& stray)
{
    if (value.is_object()) {
        if (IsMetricObject(value)) {
            if (!value["value"].is_number() && !value["value"].is_null()) {
                stray.push_back(path + ".value-is-not-a-number-or-null");
            }
            if (!value["status"].is_string()) {
                stray.push_back(path + ".status-is-not-a-string");
            }
            return;
        }
        for (auto it = value.begin(); it != value.end(); ++it) {
            const std::string child = path + "." + it.key();
            if (it.value().is_number()) {
                if (!IsFixedKey(it.key())) {
                    stray.push_back(child);
                }
            } else {
                FindStrayNumbers(it.value(), child, stray);
            }
        }
    } else if (value.is_array()) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            const std::string child = path + "[" + std::to_string(i) + "]";
            if (value[i].is_number()) {
                stray.push_back(child);
            } else {
                FindStrayNumbers(value[i], child, stray);
            }
        }
    }
}

std::set<std::string> KeysOf(const json& object)
{
    std::set<std::string> keys;
    for (auto it = object.begin(); it != object.end(); ++it) {
        keys.insert(it.key());
    }
    return keys;
}

TEST(ProfilerRoutesJsonTest, EveryNumberOfEveryRouteSitsInsideAMetric)
{
    const ProfilerSnapshot snapshot = FullSnapshot();
    const json documents[] = {
        ProfilerSnapshotToJson(snapshot),
        ProfilerFramesToJson(snapshot, 5),
        ProfilerSpikesToJson(snapshot),
    };
    for (const json& document : documents) {
        std::vector<std::string> stray;
        FindStrayNumbers(document, "", stray);
        EXPECT_TRUE(stray.empty()) << (stray.empty() ? "" : stray.front()) << " in " << document.dump().substr(0, 80);
    }
}

TEST(ProfilerRoutesJsonTest, TheExportDocumentKeepsEveryNumberInsideAMetric)
{
    Profiling::SpikeDetail pinned;
    pinned.frameIndex = 41;
    pinned.frameMs = 50.0;
    Profiling::SpikeScopeRow scope;
    scope.name = "Present";
    scope.durationMs = 30.0;
    pinned.scopes = { scope };

    const std::vector<Profiling::SpikeDetail> frozen{ pinned };
    const json document = ProfilerExportToJson(FullSnapshot(), 3, frozen);
    std::vector<std::string> stray;
    FindStrayNumbers(document, "", stray);
    EXPECT_TRUE(stray.empty()) << (stray.empty() ? "" : stray.front());
    EXPECT_EQ(document["frames"].size(), 3u);
    EXPECT_EQ(document["pinned_spikes"].size(), 1u);
}

TEST(ProfilerRoutesJsonTest, TheOverviewNamesWhyTheTimelineIsUnaligned)
{
    ProfilerSnapshot snapshot = FullSnapshot();
    Profiling::CopySnapshotText(snapshot.timeline.reason, "calibrated timestamp extension not enabled");
    const json document = ProfilerSnapshotToJson(snapshot);
    EXPECT_FALSE(document["timeline"]["aligned"].get<bool>());
    EXPECT_EQ(document["timeline"]["reason"], "calibrated timestamp extension not enabled");
}

TEST(ProfilerRoutesJsonTest, TheWalkerCatchesAStrayNumber)
{
    json document = ProfilerSnapshotToJson(FullSnapshot());
    document["gpu"]["raw_ms"] = 1.5;
    document["scopes"][0]["extra"] = 7;
    document["checks"].push_back(3);

    std::vector<std::string> stray;
    FindStrayNumbers(document, "", stray);
    EXPECT_EQ(stray.size(), 3u);
}

TEST(ProfilerRoutesJsonTest, TheOverviewKeysArePinned)
{
    const json document = ProfilerSnapshotToJson(FullSnapshot());
    const std::set<std::string> expected{
        "success", "schema_version", "frame_index", "state", "has_frame", "snapshot_age", "capture", "display_paused",
        "cpu_frame", "untracked", "blocked", "gpu", "memory", "scopes", "scope_rows_dropped", "checks", "drops",
        "device_state", "live_block", "verdict", "overhead", "timeline", "spikes_available",
    };
    EXPECT_EQ(KeysOf(document["timeline"]), (std::set<std::string>{ "aligned", "reason" }));
    EXPECT_EQ(KeysOf(document), expected);
    EXPECT_EQ(KeysOf(document["gpu"]), (std::set<std::string>{ "views", "span", "indirect_draws", "coverage" }));
    EXPECT_EQ(KeysOf(document["gpu"]["views"]), (std::set<std::string>{ "shared", "primary", "secondary" }));
    EXPECT_EQ(KeysOf(document["gpu"]["coverage"]),
        (std::set<std::string>{ "declared", "executed", "recorded", "disabled_by_toggle" }));
    EXPECT_EQ(KeysOf(document["blocked"]), (std::set<std::string>{ "gpu", "workers", "other" }));
    EXPECT_EQ(KeysOf(document["drops"]),
        (std::set<std::string>{ "late_scopes", "foreign_scopes", "lost_worker_events", "dropped_scopes", "dropped_gpu_frames" }));
    EXPECT_EQ(KeysOf(document["device_state"]),
        (std::set<std::string>{ "device_lost", "consecutive_frame_timeouts", "abandoned_objects" }));
    EXPECT_EQ(KeysOf(document["live_block"][0]), (std::set<std::string>{ "thread_slot", "site", "detail", "elapsed" }));
    EXPECT_EQ(KeysOf(document["scopes"][0]), (std::set<std::string>{ "name", "total", "self", "calls" }));
    EXPECT_EQ(KeysOf(document["checks"][0]), (std::set<std::string>{ "id", "status", "count", "text" }));
}

TEST(ProfilerRoutesJsonTest, TheMetricKeysArePinned)
{
    const json metric = MetricToJson(Ms(1.0));
    EXPECT_EQ(KeysOf(metric), (std::set<std::string>{ "value", "unit", "kind", "status", "age_frames", "reason" }));
    EXPECT_TRUE(IsMetricObject(metric));
}

TEST(ProfilerRoutesJsonTest, AMissingValueIsNullNeverZero)
{
    const json missing = MetricToJson(Profiling::MissingMetric(
        Profiling::Unit::Milliseconds, Profiling::SampleStatus::Absent, Profiling::Reason::CaptureOff));
    EXPECT_TRUE(missing["value"].is_null());
    EXPECT_EQ(missing["status"], "Absent");
    EXPECT_EQ(missing["reason"], "capture_off");

    const json present = MetricToJson(Ms(2.5));
    EXPECT_DOUBLE_EQ(present["value"].get<double>(), 2.5);
    EXPECT_EQ(present["unit"], "ms");
    EXPECT_EQ(present["status"], "Present");
}

TEST(ProfilerRoutesJsonTest, TheInactiveViewIsReportedAsInactive)
{
    const json document = ProfilerSnapshotToJson(FullSnapshot());
    EXPECT_EQ(document["gpu"]["views"]["secondary"]["status"], "Inactive");
    EXPECT_EQ(document["gpu"]["views"]["secondary"]["reason"], "no_target");
    EXPECT_EQ(document["gpu"]["views"]["primary"]["status"], "Present");
}

TEST(ProfilerRoutesJsonTest, TheLiveBlockCarriesTheWaitInProgress)
{
    const json document = ProfilerSnapshotToJson(FullSnapshot());
    ASSERT_EQ(document["live_block"].size(), 1u);
    EXPECT_EQ(document["live_block"][0]["site"], "Offscreen");
    EXPECT_EQ(document["live_block"][0]["detail"], "fence");
    EXPECT_EQ(document["live_block"][0]["thread_slot"], 3);
}

TEST(ProfilerRoutesJsonTest, FramesAreClampedToWhatTheSnapshotHolds)
{
    const ProfilerSnapshot snapshot = FullSnapshot();
    EXPECT_EQ(ProfilerFramesToJson(snapshot, 2)["frames"].size(), 2u);
    EXPECT_EQ(ProfilerFramesToJson(snapshot, 99)["frames"].size(), 3u);

    const json newest = ProfilerFramesToJson(snapshot, 1)["frames"][0];
    EXPECT_EQ(newest["frame_index"], 42);
}

TEST(ProfilerRoutesJsonTest, SpikesCarryTheirAvailability)
{
    ProfilerSnapshot snapshot = FullSnapshot();
    EXPECT_EQ(ProfilerSpikesToJson(snapshot)["spikes"].size(), 1u);
    EXPECT_EQ(ProfilerSpikesToJson(snapshot)["spikes"][0]["classification"], "unexplained");

    snapshot.spikesAvailable = false;
    snapshot.spikeCount = 0;
    const json none = ProfilerSpikesToJson(snapshot);
    EXPECT_EQ(none["spikes_available"], false);
    EXPECT_TRUE(none["spikes"].empty());
}

TEST(ProfilerRoutesParseTest, FrameCountDefaultsClampsAndRejectsText)
{
    EXPECT_EQ(ParseProfilerFrameCount(""), kProfilerFramesDefault);
    EXPECT_EQ(ParseProfilerFrameCount("5"), 5u);
    EXPECT_EQ(ParseProfilerFrameCount("0"), 1u);
    EXPECT_EQ(ParseProfilerFrameCount("100000"), kProfilerFramesMax);
    EXPECT_FALSE(ParseProfilerFrameCount("abc"));
    EXPECT_FALSE(ParseProfilerFrameCount("-3"));
    EXPECT_FALSE(ParseProfilerFrameCount("5x"));
}

TEST(ProfilerRoutesParseTest, SetNeedsAtLeastOneSwitch)
{
    EXPECT_FALSE(ParseProfilerSet("", "", "").valid);
    EXPECT_FALSE(ParseProfilerSet("maybe", "", "").valid);
    EXPECT_FALSE(ParseProfilerSet("", "2", "").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "not json").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "[1]").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", R"({"capture":"yes"})").valid);
}

TEST(ProfilerRoutesParseTest, SetReadsQueryValuesAndABody)
{
    const ParsedProfilerSet query = ParseProfilerSet("false", "1", "");
    ASSERT_TRUE(query.valid);
    EXPECT_EQ(query.capture, std::optional<bool>(false));
    EXPECT_EQ(query.displayPause, std::optional<bool>(true));

    const ParsedProfilerSet body = ParseProfilerSet("", "", R"({"capture":true,"display_pause":false})");
    ASSERT_TRUE(body.valid);
    EXPECT_EQ(body.capture, std::optional<bool>(true));
    EXPECT_EQ(body.displayPause, std::optional<bool>(false));

    // A query value wins over the body.
    const ParsedProfilerSet mixed = ParseProfilerSet("0", "", R"({"capture":true})");
    ASSERT_TRUE(mixed.valid);
    EXPECT_EQ(mixed.capture, std::optional<bool>(false));
    EXPECT_FALSE(mixed.displayPause.has_value());
}

TEST(ProfilerRoutesParseTest, PinUnpinAndThresholdAreParsedStrictly)
{
    const ParsedProfilerSet pin = ParseProfilerSet("", "", "", "41", "7", "12.5");
    ASSERT_TRUE(pin.valid);
    EXPECT_EQ(pin.pin, std::optional<std::uint64_t>(41));
    EXPECT_EQ(pin.unpin, std::optional<std::uint64_t>(7));
    ASSERT_TRUE(pin.spikeThresholdMs.has_value());
    EXPECT_FLOAT_EQ(*pin.spikeThresholdMs, 12.5f);

    EXPECT_FALSE(ParseProfilerSet("", "", "", "-1").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "", "4x").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "", "", "", "nan").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "", "", "", "inf").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "", "", "", "-2").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "", "", "", "20000").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", "", "", "", " 5").valid);

    const ParsedProfilerSet body = ParseProfilerSet("", "", R"({"pin":9,"spike_threshold_ms":0})");
    ASSERT_TRUE(body.valid);
    EXPECT_EQ(body.pin, std::optional<std::uint64_t>(9));
    EXPECT_FLOAT_EQ(*body.spikeThresholdMs, 0.0f);
    EXPECT_FALSE(ParseProfilerSet("", "", R"({"pin":"9"})").valid);
    EXPECT_FALSE(ParseProfilerSet("", "", R"({"pin":-3})").valid);
}

TEST(ProfilerRoutesParseTest, ASpikeIndexIsAWholeFrameNumber)
{
    EXPECT_EQ(ParseSpikeFrameIndex("0"), std::optional<std::uint64_t>(0));
    EXPECT_EQ(ParseSpikeFrameIndex("123456789012"), std::optional<std::uint64_t>(123456789012ull));
    EXPECT_FALSE(ParseSpikeFrameIndex(""));
    EXPECT_FALSE(ParseSpikeFrameIndex("abc"));
    EXPECT_FALSE(ParseSpikeFrameIndex("1.5"));
    EXPECT_FALSE(ParseSpikeFrameIndex("-1"));
}

Profiling::SpikeDetail FullSpikeDetail()
{
    Profiling::SpikeDetail detail;
    detail.frameIndex = 41;
    detail.frameMs = 50.0;
    detail.medianMs = 16.0;
    detail.pinned = true;
    detail.classification.kind = Profiling::SpikeClass::Explained;
    detail.classification.excessMs = 34.0;
    detail.classification.dominantSelfMs = 30.0;
    detail.cause = "GpuWait(Offscreen:fence)";
    detail.gpu.span = { 9.0f, Profiling::SampleStatus::Present, 0 };
    detail.droppedScopes = 2;
    detail.scopes.push_back({ "Present", 0.0, 50.0, 20.0, 0, Profiling::ScopeKind::Work });
    detail.scopes.push_back({ "GpuWait(Offscreen:fence)", 1.0, 30.0, 30.0, 1, Profiling::ScopeKind::Blocked });
    return detail;
}

TEST(ProfilerRoutesJsonTest, ASpikeDetailKeepsEveryNumberInsideAMetric)
{
    const json document = ProfilerSpikeDetailToJson(FullSpikeDetail());
    std::vector<std::string> stray;
    FindStrayNumbers(document, "", stray);
    EXPECT_TRUE(stray.empty()) << (stray.empty() ? "" : stray.front());

    EXPECT_EQ(KeysOf(document),
        (std::set<std::string>{ "success", "schema_version", "frame_index", "state", "pinned", "gpu_attached", "classification",
            "cause", "marker_present", "frame", "median", "excess", "dominant_self", "gpu_span", "gpu_views", "dropped_scopes",
            "scopes" }));
    ASSERT_EQ(document["scopes"].size(), 2u);
    EXPECT_EQ(KeysOf(document["scopes"][0]), (std::set<std::string>{ "name", "kind", "depth", "start", "duration", "self" }));
    EXPECT_EQ(document["scopes"][1]["kind"], "blocked");
    EXPECT_EQ(document["classification"], "explained");
}

TEST(ProfilerRoutesJsonTest, TheSpikeListKeysArePinned)
{
    const json document = ProfilerSpikesToJson(FullSnapshot());
    EXPECT_EQ(KeysOf(document["spikes"][0]),
        (std::set<std::string>{ "frame_index", "cpu", "median", "excess", "dominant_self", "gpu_span", "classification", "cause",
            "marker_present", "pinned", "gpu_attached" }));
    EXPECT_TRUE(IsMetricObject(document["pin_limit"]));
    EXPECT_EQ(document["pin_limit"]["value"], 8);
}

TEST(ProfilerRoutesJsonTest, TheVerdictAndOverheadKeysArePinned)
{
    const json document = ProfilerSnapshotToJson(FullSnapshot());
    EXPECT_EQ(KeysOf(document["verdict"]),
        (std::set<std::string>{ "text", "kind", "frames", "frame", "cpu_work", "gpu_wait", "present_wait", "other_wait", "untracked",
            "stall_share", "stall_site" }));
    EXPECT_EQ(KeysOf(document["overhead"]), (std::set<std::string>{ "cost_per_event", "estimated", "share", "warn" }));
    EXPECT_EQ(document["verdict"]["kind"], "unknown");
}

TEST(ProfilerRoutesReplyTest, ASpikeThatIsGoneAnswers404AndAFrozenOneAnswers200)
{
    EXPECT_EQ(BuildProfilerSpikeReply(std::nullopt, 5).status, 404);
    const ProfilerReply found = BuildProfilerSpikeReply(FullSpikeDetail(), 41);
    EXPECT_EQ(found.status, 200);
    EXPECT_EQ(json::parse(found.body)["frame_index"], 41);
}

TEST(ProfilerRoutesReplyTest, ARefusedPinNamesTheLimitOrTheMissingFrame)
{
    const ProfilerReply limit = BuildProfilerPinErrorReply(true, 3, 8);
    EXPECT_EQ(limit.status, 409);
    EXPECT_NE(limit.body.find("pin limit"), std::string::npos);

    const ProfilerReply missing = BuildProfilerPinErrorReply(false, 3, 1);
    EXPECT_EQ(missing.status, 404);
    EXPECT_NE(missing.body.find("frame 3"), std::string::npos);
}

TEST(ProfilerRoutesReplyTest, TheSetReplyCarriesSpikeStateAsMetrics)
{
    ProfilerSetState state;
    state.pinnedSpikes = 2;
    state.spikeThresholdMs = 7.5f;
    const json reply = json::parse(BuildProfilerSetReplyJson(state));
    EXPECT_EQ(reply["pinned_spikes"]["value"], 2);
    EXPECT_DOUBLE_EQ(reply["spike_threshold"]["value"].get<double>(), 7.5);
    std::vector<std::string> stray;
    FindStrayNumbers(reply, "", stray);
    EXPECT_TRUE(stray.empty());
}

TEST(ProfilerRoutesReplyTest, NoSnapshotAnswers503)
{
    const ProfilerReply reply = BuildProfilerReply(ProfilerRoute::Overview, std::nullopt, 10);
    EXPECT_EQ(reply.status, 503);
    EXPECT_EQ(json::parse(reply.body)["success"], false);
}

TEST(ProfilerRoutesReplyTest, EachRouteAnswersItsOwnDocument)
{
    const std::optional<ProfilerSnapshot> snapshot = FullSnapshot();
    EXPECT_TRUE(json::parse(BuildProfilerReply(ProfilerRoute::Overview, snapshot, 10).body).contains("cpu_frame"));
    EXPECT_TRUE(json::parse(BuildProfilerReply(ProfilerRoute::Frames, snapshot, 10).body).contains("frames"));
    EXPECT_TRUE(json::parse(BuildProfilerReply(ProfilerRoute::Spikes, snapshot, 10).body).contains("spikes"));
}

TEST(ProfilerRoutesReplyTest, MissingBridgeOrCompiledOutProfilerIsNotACrash)
{
    const ProfilerReply reply = BuildProfilerUnavailableReply(false);
#if GTE_ENABLE_PROFILER
    EXPECT_EQ(reply.status, 503);
#else
    EXPECT_EQ(reply.status, 200);
    EXPECT_EQ(json::parse(reply.body)["state"], "compiled_out");
#endif
    EXPECT_EQ(json::parse(reply.body).contains("success"), true);
}

TEST(ProfilerRoutesReplyTest, SetReplyShowsTheStateAfterTheChange)
{
    const json reply = json::parse(BuildProfilerSetReplyJson(false, true));
    EXPECT_EQ(reply["success"], true);
    EXPECT_EQ(reply["capture"], false);
    EXPECT_EQ(reply["display_paused"], true);
}

TEST(ProfilerBridgeTest, ASnapshotIsBuiltOnlyAfterSomeoneAsked)
{
    ProfilerSnapshotBridge bridge;
    EXPECT_FALSE(bridge.WantsSnapshot());

    EXPECT_FALSE(bridge.Latest(std::chrono::milliseconds(0)).has_value()); // nothing published yet
    EXPECT_TRUE(bridge.WantsSnapshot());

    const std::uint64_t later = ProfilerSnapshotBridge::NowNanoseconds() + ProfilerSnapshotBridge::kAskWindowNanoseconds + 1;
    EXPECT_FALSE(bridge.WantsSnapshotAt(later));
}

TEST(ProfilerBridgeTest, LatestReturnsTheCopyWithItsAgeAndLiveWaits)
{
    ProfilerSnapshotBridge bridge;
    ProfilerSnapshot snapshot;
    snapshot.frameIndex = 7;
    bridge.Publish(snapshot);

    Profiling::LiveBlockTable& live = Profiling::FrameProfiler::Instance().LiveBlocks();
    live.Begin("Shutdown", "test", Profiling::NowMicroseconds());
    const std::optional<ProfilerSnapshot> copy = bridge.Latest(std::chrono::milliseconds(0));
    live.End();

    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(copy->frameIndex, 7u);
    EXPECT_EQ(copy->snapshotAge.status, Profiling::SampleStatus::Present);
    EXPECT_EQ(copy->snapshotAge.kind, Profiling::MetricKind::Derived);
    ASSERT_EQ(copy->liveBlockCount, 1u);
    EXPECT_EQ(Profiling::SnapshotText(copy->liveBlocks[0].site), "Shutdown");
}

TEST(ProfilerBridgeTest, AReaderWaitsForAFreshPublish)
{
    ProfilerSnapshotBridge bridge;
    ProfilerSnapshot snapshot;
    snapshot.frameIndex = 1;
    bridge.Publish(snapshot);
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // older than kFreshNanoseconds

    std::thread publisher([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        ProfilerSnapshot next;
        next.frameIndex = 2;
        bridge.Publish(next);
    });
    const std::optional<ProfilerSnapshot> copy = bridge.Latest(std::chrono::milliseconds(2000));
    publisher.join();

    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(copy->frameIndex, 2u);
}

TEST(ProfilerBridgeTest, ABlockedMainThreadStillGivesTheLastSnapshot)
{
    ProfilerSnapshotBridge bridge;
    ProfilerSnapshot snapshot;
    snapshot.frameIndex = 5;
    bridge.Publish(snapshot);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto start = std::chrono::steady_clock::now();
    const std::optional<ProfilerSnapshot> copy = bridge.Latest(std::chrono::milliseconds(100));
    const auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(copy->frameIndex, 5u);
    EXPECT_GT(copy->snapshotAge.value, 250.0); // the age tells the reader it is old
    EXPECT_LT(elapsed, std::chrono::milliseconds(1500));
}

// Every field of the snapshot carries the same number, so a torn read shows as a mismatch.
ProfilerSnapshot EncodedSnapshot(std::uint64_t n)
{
    ProfilerSnapshot s;
    s.frameIndex = n;
    s.cpuFrame = Ms(static_cast<double>(n));
    s.scopeCount = 1;
    s.scopes[0].total = Ms(static_cast<double>(n));
    s.frames[0].frameIndex = n;
    s.frames[Profiling::kSnapshotFrameRows - 1].frameIndex = n;
    s.device.abandonedObjects = Cnt(static_cast<double>(n));
    s.spikes[Profiling::kSnapshotSpikeRows - 1].frameIndex = n;
    return s;
}

bool IsConsistent(const ProfilerSnapshot& s)
{
    const std::uint64_t n = s.frameIndex;
    return s.cpuFrame.value == static_cast<double>(n) && s.scopes[0].total.value == static_cast<double>(n)
        && s.frames[0].frameIndex == n && s.frames[Profiling::kSnapshotFrameRows - 1].frameIndex == n
        && s.device.abandonedObjects.value == static_cast<double>(n)
        && s.spikes[Profiling::kSnapshotSpikeRows - 1].frameIndex == n;
}

TEST(ProfilerBridgeTest, AReaderAlwaysSeesOneConsistentSnapshotWhileTheMainThreadPublishes)
{
    constexpr std::uint64_t kIterations = 10000;
    ProfilerSnapshotBridge bridge;
    bridge.Publish(EncodedSnapshot(0));

    std::atomic<bool> done{ false };
    std::atomic<std::uint64_t> torn{ 0 };
    std::atomic<std::uint64_t> reads{ 0 };

    std::thread reader([&] {
        std::uint64_t lastSeen = 0;
        while (!done.load()) {
            const std::optional<ProfilerSnapshot> copy = bridge.Latest(std::chrono::milliseconds(0));
            if (!copy) {
                continue;
            }
            ++reads;
            if (!IsConsistent(*copy) || copy->frameIndex < lastSeen) {
                ++torn;
            }
            lastSeen = copy->frameIndex;
        }
    });

    for (std::uint64_t i = 1; i <= kIterations; ++i) {
        bridge.Publish(EncodedSnapshot(i));
    }
    done = true;
    reader.join();

    EXPECT_EQ(torn.load(), 0u);
    EXPECT_GT(reads.load(), 0u);
}

} // namespace
} // namespace gte::Network
