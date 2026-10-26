#pragma once

// HTTP face of the profiler: GET /profiler, /profiler/frames, /profiler/spikes, /profiler/spike and POST /profiler/set.
// Every number in a reply sits inside a metric object, except the keys in kProfilerFixedNumberKeys.

#include "../Profiling/ProfilerSnapshot.h"
#include "../Profiling/SpikeDetail.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace httplib {
class Server;
struct Request;
struct Response;
} // namespace httplib

namespace gte {
class ProfilerSnapshotBridge;
} // namespace gte

namespace gte::Network {

// Plain numbers allowed outside a metric object.
inline constexpr std::array<std::string_view, 4> kProfilerFixedNumberKeys{
    "schema_version",
    "frame_index",
    "thread_slot",
    "depth",
};

// Keys every metric object carries.
inline constexpr std::array<std::string_view, 6> kProfilerMetricKeys{
    "value", "unit", "kind", "status", "age_frames", "reason",
};

inline constexpr std::size_t kProfilerFramesDefault = 30;
inline constexpr std::size_t kProfilerFramesMax = Profiling::kSnapshotFrameRows;

struct ProfilerReply {
    int status = 200;
    std::string body;
};

// {"value","unit","kind","status","age_frames","reason"}.
nlohmann::json MetricToJson(const Profiling::Metric& metric);

// Body of GET /profiler.
nlohmann::json ProfilerSnapshotToJson(const Profiling::ProfilerSnapshot& snapshot);

// Body of GET /profiler/frames: the newest `count` frames, oldest first.
nlohmann::json ProfilerFramesToJson(const Profiling::ProfilerSnapshot& snapshot, std::size_t count);

// Body of GET /profiler/spikes.
nlohmann::json ProfilerSpikesToJson(const Profiling::ProfilerSnapshot& snapshot);

// Empty text means the default; anything else must be a whole number, clamped to 1..kProfilerFramesMax.
std::optional<std::size_t> ParseProfilerFrameCount(const std::string& text);

enum class ProfilerRoute : std::uint8_t { Overview, Frames, Spikes };

// A snapshot that does not exist yet answers 503. A profiler that is compiled out answers a status object.
ProfilerReply BuildProfilerReply(
    ProfilerRoute route, const std::optional<Profiling::ProfilerSnapshot>& snapshot, std::size_t frameCount);

// Reply for a route whose bridge is missing (503) or whose profiler is compiled out (status object).
ProfilerReply BuildProfilerUnavailableReply(bool bridgePresent);

// Parsed POST /profiler/set. Every field may be left out, but at least one must be given.
struct ParsedProfilerSet {
    bool valid = false;
    std::string errorMessage;
    std::optional<bool> capture;
    std::optional<bool> displayPause;
    std::optional<std::uint64_t> pin;   // frame index of a frozen spike
    std::optional<std::uint64_t> unpin; // frame index of a frozen spike
    std::optional<float> spikeThresholdMs; // 0 selects the median rule
};

// Query/form values come first; a JSON body {"capture","display_pause","pin","unpin","spike_threshold_ms"} fills what
// they leave out.
ParsedProfilerSet ParseProfilerSet(const std::string& captureText, const std::string& displayPauseText,
    const std::string& body, const std::string& pinText = {}, const std::string& unpinText = {},
    const std::string& spikeThresholdText = {});

// State after a set request.
struct ProfilerSetState {
    bool capture = true;
    bool displayPaused = false;
    std::size_t pinnedSpikes = 0;
    float spikeThresholdMs = 0.0f;
};

// Body of POST /profiler/set: the switch state after the change.
std::string BuildProfilerSetReplyJson(const ProfilerSetState& state);
std::string BuildProfilerSetReplyJson(bool capture, bool displayPaused);

// Whole-number frame index of a frozen spike (query `index`).
std::optional<std::uint64_t> ParseSpikeFrameIndex(const std::string& text);

// Body of GET /profiler/spike: one frozen frame with its scope tree.
nlohmann::json ProfilerSpikeDetailToJson(const Profiling::SpikeDetail& detail);

// 200 with the frame, 404 when it is not frozen (never was, or evicted).
ProfilerReply BuildProfilerSpikeReply(const std::optional<Profiling::SpikeDetail>& detail, std::uint64_t frameIndex);

// Outcome text of a failed pin or unpin: status 404 for an unknown frame, 409 for the pin limit.
ProfilerReply BuildProfilerPinErrorReply(bool limitReached, std::uint64_t frameIndex, std::size_t pinned);

// Refuses a browser request with 403 and returns true.
using BrowserRefusal = bool (*)(const httplib::Request&, httplib::Response&);

// Registers the five routes. A null bridge makes them answer 503.
void RegisterProfilerRoutes(httplib::Server& server, ProfilerSnapshotBridge* bridge, BrowserRefusal refuseBrowser);

} // namespace gte::Network
