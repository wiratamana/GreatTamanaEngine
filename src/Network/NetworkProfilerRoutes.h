#pragma once

// HTTP face of the profiler (/profiler routes). Every number sits inside a metric object,
// except the keys in kProfilerFixedNumberKeys.

#include "../Profiling/ProfilerSnapshot.h"
#include "../Profiling/SpikeDetail.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

// 200 with the document of `route`.
ProfilerReply BuildProfilerReply(ProfilerRoute route, const Profiling::ProfilerSnapshot& snapshot, std::size_t frameCount);

// 200 with state "no_snapshot" and the waits in progress, for a main thread that never published (e.g. it is stalled).
ProfilerReply BuildProfilerNoSnapshotReply(const Profiling::ProfilerSnapshot& liveOnly);

// Reply when the bridge is missing (503), or a status object (200) when the profiler is compiled out.
ProfilerReply BuildProfilerUnavailableReply();

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

// Whole-number frame index of a frozen spike (query `index`).
std::optional<std::uint64_t> ParseSpikeFrameIndex(const std::string& text);

// Body of GET /profiler/spike: one frozen frame with its scope tree.
nlohmann::json ProfilerSpikeDetailToJson(const Profiling::SpikeDetail& detail);

// 200 with the frame, 404 when it is not frozen (never was, or evicted).
ProfilerReply BuildProfilerSpikeReply(const std::optional<Profiling::SpikeDetail>& detail, std::uint64_t frameIndex);

// Outcome text of a failed pin or unpin: status 404 for an unknown frame, 409 for the pin limit.
ProfilerReply BuildProfilerPinErrorReply(bool limitReached, std::uint64_t frameIndex, std::size_t pinned);

// Body of POST /profiler/export: the overview, the newest `count` frames and the frozen frames that are pinned.
nlohmann::json ProfilerExportToJson(
    const Profiling::ProfilerSnapshot& snapshot, std::size_t count, std::span<const Profiling::SpikeDetail> pinned);

// A reply that carries bytes, e.g. an image. `contentType` is empty for a JSON error body.
struct ProfilerImageReply {
    int status = 200;
    std::string contentType;
    std::string body;
};

// PNG of one frame's timeline. A frame that does not exist answers 503 (newest) or 404 (a named frame).
ProfilerImageReply BuildProfilerTimelineReply(
    const std::optional<Profiling::SpikeDetail>& frame, bool namedFrame, std::uint64_t frameIndex, int width);

// Refuses a browser request with 403 and returns true.
using BrowserRefusal = bool (*)(const httplib::Request&, httplib::Response&);

// Registers the profiler routes. A null bridge answers 503 when the profiler is on, and a compiled_out status
// object (200) when it is off.
void RegisterProfilerRoutes(httplib::Server& server, ProfilerSnapshotBridge* bridge, BrowserRefusal refuseBrowser);

} // namespace gte::Network
