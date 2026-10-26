#pragma once

// HTTP face of the profiler: GET /profiler, /profiler/frames, /profiler/spikes and POST /profiler/set.
// Every number in a reply sits inside a metric object, except the keys in kProfilerFixedNumberKeys.

#include "../Profiling/ProfilerSnapshot.h"

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
inline constexpr std::array<std::string_view, 3> kProfilerFixedNumberKeys{
    "schema_version",
    "frame_index",
    "thread_slot",
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

// Parsed POST /profiler/set. Either switch may be left out.
struct ParsedProfilerSet {
    bool valid = false;
    std::string errorMessage;
    std::optional<bool> capture;
    std::optional<bool> displayPause;
};

// Query/form values come first; a JSON body {"capture":bool,"display_pause":bool} fills what they leave out.
ParsedProfilerSet ParseProfilerSet(const std::string& captureText, const std::string& displayPauseText, const std::string& body);

// Body of POST /profiler/set: the switch state after the change.
std::string BuildProfilerSetReplyJson(bool capture, bool displayPaused);

// Refuses a browser request with 403 and returns true.
using BrowserRefusal = bool (*)(const httplib::Request&, httplib::Response&);

// Registers the four routes. A null bridge makes them answer 503.
void RegisterProfilerRoutes(httplib::Server& server, ProfilerSnapshotBridge* bridge, BrowserRefusal refuseBrowser);

} // namespace gte::Network
