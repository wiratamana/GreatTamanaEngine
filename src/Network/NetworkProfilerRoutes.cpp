#include "NetworkProfilerRoutes.h"

#include "NetworkRoutes.h"

#include "../Application/ProfilerSnapshotBridge.h"
#include "../Profiling/ProfilerSnapshotBuilder.h"

#include <httplib.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <system_error>

namespace gte::Network {

namespace {

using Profiling::Metric;
using Profiling::ProfilerSnapshot;
using Profiling::SampleStatus;

constexpr std::chrono::milliseconds kSnapshotWait{ 500 };

const char* ToString(Profiling::CheckId id) noexcept
{
    switch (id) {
    case Profiling::CheckId::CaptureOn: return "capture_on";
    case Profiling::CheckId::ScopesDropped: return "scopes_dropped";
    case Profiling::CheckId::LateScopes: return "late_scopes";
    case Profiling::CheckId::ForeignScopes: return "foreign_scopes";
    case Profiling::CheckId::WorkerEvents: return "worker_events";
    case Profiling::CheckId::Reconciliation: return "reconciliation";
    case Profiling::CheckId::ScopeNesting: return "scope_nesting";
    }
    return "unknown";
}

const char* ToString(Profiling::CheckStatus status) noexcept
{
    switch (status) {
    case Profiling::CheckStatus::Ok: return "ok";
    case Profiling::CheckStatus::Warn: return "warn";
    case Profiling::CheckStatus::Fail: return "fail";
    }
    return "unknown";
}

const char* ViewLabel(std::size_t slot) noexcept
{
    switch (static_cast<Profiling::ViewSlot>(slot)) {
    case Profiling::ViewSlot::Shared: return "shared";
    case Profiling::ViewSlot::Primary: return "primary";
    case Profiling::ViewSlot::Secondary: return "secondary";
    }
    return "unknown";
}

std::string Text(std::span<const char> text)
{
    return std::string(Profiling::SnapshotText(text));
}

bool HasValue(SampleStatus status) noexcept
{
    return status == SampleStatus::Present || status == SampleStatus::Partial || status == SampleStatus::Stale;
}

nlohmann::json PerView(const std::array<Metric, Profiling::kViewSlotCount>& metrics)
{
    nlohmann::json out = nlohmann::json::object();
    for (std::size_t i = 0; i < metrics.size(); ++i) {
        out[ViewLabel(i)] = MetricToJson(metrics[i]);
    }
    return out;
}

nlohmann::json Envelope(const ProfilerSnapshot& snapshot)
{
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["schema_version"] = snapshot.schemaVersion;
    out["frame_index"] = snapshot.frameIndex;
    out["state"] = "ok";
    out["has_frame"] = snapshot.hasFrame;
    out["snapshot_age"] = MetricToJson(snapshot.snapshotAge);
    return out;
}

bool ParseBoolText(const std::string& text, bool& out)
{
    if (text == "true" || text == "1") {
        out = true;
        return true;
    }
    if (text == "false" || text == "0") {
        out = false;
        return true;
    }
    return false;
}

} // namespace

nlohmann::json MetricToJson(const Metric& metric)
{
    nlohmann::json out = nlohmann::json::object();
    // A missing value is null, never 0.
    out["value"] = HasValue(metric.status) ? nlohmann::json(metric.value) : nlohmann::json(nullptr);
    out["unit"] = Profiling::ToString(metric.unit);
    out["kind"] = Profiling::ToString(metric.kind);
    out["status"] = Profiling::ToString(metric.status);
    out["age_frames"] = metric.ageFrames;
    out["reason"] = Profiling::ToString(metric.reason);
    return out;
}

nlohmann::json ProfilerSnapshotToJson(const ProfilerSnapshot& snapshot)
{
    nlohmann::json out = Envelope(snapshot);
    out["capture"] = snapshot.captureOn;
    out["display_paused"] = snapshot.displayPaused;

    out["cpu_frame"] = MetricToJson(snapshot.cpuFrame);
    out["untracked"] = MetricToJson(snapshot.untracked);

    nlohmann::json blocked = nlohmann::json::object();
    blocked["gpu"] = MetricToJson(snapshot.blockedGpu);
    blocked["workers"] = MetricToJson(snapshot.blockedWorkers);
    blocked["other"] = MetricToJson(snapshot.blockedOther);
    out["blocked"] = std::move(blocked);

    nlohmann::json gpu = nlohmann::json::object();
    gpu["views"] = PerView(snapshot.gpuView);
    gpu["span"] = MetricToJson(snapshot.gpuSpan);
    gpu["indirect_draws"] = PerView(snapshot.indirectDraws);
    nlohmann::json coverage = nlohmann::json::object();
    coverage["declared"] = MetricToJson(snapshot.gpuCoverage.declared);
    coverage["executed"] = MetricToJson(snapshot.gpuCoverage.executed);
    coverage["recorded"] = MetricToJson(snapshot.gpuCoverage.recorded);
    coverage["disabled_by_toggle"] = MetricToJson(snapshot.gpuCoverage.disabledByToggle);
    gpu["coverage"] = std::move(coverage);
    out["gpu"] = std::move(gpu);

    nlohmann::json memory = nlohmann::json::object();
    memory["total"] = MetricToJson(snapshot.memoryTotal);
    out["memory"] = std::move(memory);

    nlohmann::json scopes = nlohmann::json::array();
    for (std::uint32_t i = 0; i < snapshot.scopeCount && i < snapshot.scopes.size(); ++i) {
        const Profiling::SnapshotScopeRow& row = snapshot.scopes[i];
        nlohmann::json item = nlohmann::json::object();
        item["name"] = Text(row.name);
        item["total"] = MetricToJson(row.total);
        item["self"] = MetricToJson(row.self);
        item["calls"] = MetricToJson(row.calls);
        scopes.push_back(std::move(item));
    }
    out["scopes"] = std::move(scopes);
    out["scope_rows_dropped"] = MetricToJson(Profiling::PresentMetric(snapshot.scopeRowsDropped, Profiling::Unit::Count));

    nlohmann::json checks = nlohmann::json::array();
    for (const Profiling::SnapshotCheck& check : snapshot.checks) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = ToString(check.id);
        item["status"] = ToString(check.status);
        item["count"] = MetricToJson(check.count);
        item["text"] = check.text != nullptr ? check.text : "";
        checks.push_back(std::move(item));
    }
    out["checks"] = std::move(checks);

    nlohmann::json drops = nlohmann::json::object();
    drops["late_scopes"] = MetricToJson(snapshot.drops.lateScopes);
    drops["foreign_scopes"] = MetricToJson(snapshot.drops.foreignScopes);
    drops["lost_worker_events"] = MetricToJson(snapshot.drops.lostWorkerEvents);
    drops["dropped_scopes"] = MetricToJson(snapshot.drops.droppedScopes);
    drops["dropped_gpu_frames"] = MetricToJson(snapshot.drops.droppedGpuFrames);
    out["drops"] = std::move(drops);

    nlohmann::json device = nlohmann::json::object();
    device["device_lost"] = snapshot.device.deviceLost;
    device["consecutive_frame_timeouts"] = MetricToJson(snapshot.device.consecutiveFrameTimeouts);
    device["abandoned_objects"] = MetricToJson(snapshot.device.abandonedObjects);
    out["device_state"] = std::move(device);

    nlohmann::json live = nlohmann::json::array();
    for (std::uint32_t i = 0; i < snapshot.liveBlockCount && i < snapshot.liveBlocks.size(); ++i) {
        const Profiling::SnapshotLiveBlock& block = snapshot.liveBlocks[i];
        nlohmann::json item = nlohmann::json::object();
        item["thread_slot"] = block.threadSlot;
        item["site"] = Text(block.site);
        item["detail"] = Text(block.detail);
        item["elapsed"] = MetricToJson(block.elapsed);
        live.push_back(std::move(item));
    }
    out["live_block"] = std::move(live);

    nlohmann::json verdict = nlohmann::json::object();
    verdict["text"] = Text(snapshot.verdict);
    out["verdict"] = std::move(verdict);

    out["spikes_available"] = snapshot.spikesAvailable;
    return out;
}

nlohmann::json ProfilerFramesToJson(const ProfilerSnapshot& snapshot, std::size_t count)
{
    nlohmann::json out = Envelope(snapshot);
    const std::size_t available = std::min<std::size_t>(snapshot.frameCount, snapshot.frames.size());
    const std::size_t shown = std::min(count, available);

    nlohmann::json frames = nlohmann::json::array();
    for (std::size_t i = available - shown; i < available; ++i) {
        const Profiling::SnapshotFrameRow& row = snapshot.frames[i];
        nlohmann::json item = nlohmann::json::object();
        item["frame_index"] = row.frameIndex;
        item["cpu"] = MetricToJson(row.cpu);
        item["gpu_span"] = MetricToJson(row.gpuSpan);
        item["gpu_primary"] = MetricToJson(row.gpuPrimary);
        item["gpu_secondary"] = MetricToJson(row.gpuSecondary);
        frames.push_back(std::move(item));
    }
    out["frames"] = std::move(frames);
    return out;
}

nlohmann::json ProfilerSpikesToJson(const ProfilerSnapshot& snapshot)
{
    nlohmann::json out = Envelope(snapshot);
    out["spikes_available"] = snapshot.spikesAvailable;

    nlohmann::json spikes = nlohmann::json::array();
    for (std::uint32_t i = 0; i < snapshot.spikeCount && i < snapshot.spikes.size(); ++i) {
        const Profiling::SnapshotSpikeRow& row = snapshot.spikes[i];
        nlohmann::json item = nlohmann::json::object();
        item["frame_index"] = row.frameIndex;
        item["cpu"] = MetricToJson(row.cpu);
        item["classification"] = Text(row.classification);
        spikes.push_back(std::move(item));
    }
    out["spikes"] = std::move(spikes);
    return out;
}

std::optional<std::size_t> ParseProfilerFrameCount(const std::string& text)
{
    if (text.empty()) {
        return kProfilerFramesDefault;
    }
    std::uint64_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::clamp<std::uint64_t>(value, 1, kProfilerFramesMax));
}

ProfilerReply BuildProfilerReply(
    ProfilerRoute route, const std::optional<ProfilerSnapshot>& snapshot, std::size_t frameCount)
{
    if (!snapshot) {
        return { 503, BuildGenericErrorResponseJson("no profiler snapshot yet - the main thread has not published one, try again") };
    }
    switch (route) {
    case ProfilerRoute::Overview: return { 200, ProfilerSnapshotToJson(*snapshot).dump() };
    case ProfilerRoute::Frames: return { 200, ProfilerFramesToJson(*snapshot, frameCount).dump() };
    case ProfilerRoute::Spikes: return { 200, ProfilerSpikesToJson(*snapshot).dump() };
    }
    return { 500, BuildGenericErrorResponseJson("unknown profiler route") };
}

ProfilerReply BuildProfilerUnavailableReply(bool bridgePresent)
{
#if GTE_ENABLE_PROFILER
    if (!bridgePresent) {
        return { 503, BuildGenericErrorResponseJson("profiler bridge not available") };
    }
    return { 500, BuildGenericErrorResponseJson("profiler is available") };
#else
    static_cast<void>(bridgePresent);
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["schema_version"] = Profiling::kProfilerSchemaVersion;
    out["state"] = "compiled_out";
    return { 200, out.dump() };
#endif
}

ParsedProfilerSet ParseProfilerSet(const std::string& captureText, const std::string& displayPauseText, const std::string& body)
{
    ParsedProfilerSet result;
    bool flag = false;

    if (!captureText.empty()) {
        if (!ParseBoolText(captureText, flag)) {
            result.errorMessage = "invalid parameter: capture - must be true, false, 1 or 0";
            return result;
        }
        result.capture = flag;
    }
    if (!displayPauseText.empty()) {
        if (!ParseBoolText(displayPauseText, flag)) {
            result.errorMessage = "invalid parameter: display_pause - must be true, false, 1 or 0";
            return result;
        }
        result.displayPause = flag;
    }

    if (!body.empty()) {
        const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            result.errorMessage = "invalid body: expected a JSON object";
            return result;
        }
        const auto read = [&](const char* key, std::optional<bool>& target) {
            const auto it = json.find(key);
            if (it == json.end()) {
                return true;
            }
            if (!it->is_boolean()) {
                result.errorMessage = std::string("invalid body: ") + key + " must be a boolean";
                return false;
            }
            if (!target) {
                target = it->get<bool>();
            }
            return true;
        };
        if (!read("capture", result.capture) || !read("display_pause", result.displayPause)) {
            return result;
        }
    }

    if (!result.capture && !result.displayPause) {
        result.errorMessage = "nothing to set: pass capture and/or display_pause";
        return result;
    }
    result.valid = true;
    return result;
}

std::string BuildProfilerSetReplyJson(bool capture, bool displayPaused)
{
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["capture"] = capture;
    out["display_paused"] = displayPaused;
    return out.dump();
}

void RegisterProfilerRoutes(httplib::Server& server, ProfilerSnapshotBridge* bridge, BrowserRefusal refuseBrowser)
{
    const auto serve = [bridge](ProfilerRoute route) {
        return [bridge, route](const httplib::Request& req, httplib::Response& res) {
            ProfilerReply reply;
#if GTE_ENABLE_PROFILER
            std::size_t frameCount = kProfilerFramesDefault;
            if (route == ProfilerRoute::Frames) {
                const std::optional<std::size_t> parsed = ParseProfilerFrameCount(req.get_param_value("count"));
                if (!parsed) {
                    res.status = 400;
                    res.set_content(BuildGenericErrorResponseJson("invalid query parameter: count - must be a whole number"),
                        "application/json");
                    return;
                }
                frameCount = *parsed;
            }
            if (bridge == nullptr) {
                reply = BuildProfilerUnavailableReply(false);
            } else {
                reply = BuildProfilerReply(route, bridge->Latest(kSnapshotWait), frameCount);
            }
#else
            static_cast<void>(req);
            static_cast<void>(route);
            reply = BuildProfilerUnavailableReply(bridge != nullptr);
#endif
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
        };
    };

    server.Get("/profiler", serve(ProfilerRoute::Overview));
    server.Get("/profiler/frames", serve(ProfilerRoute::Frames));
    server.Get("/profiler/spikes", serve(ProfilerRoute::Spikes));

    server.Post("/profiler/set", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser != nullptr && refuseBrowser(req, res)) {
            return;
        }
#if GTE_ENABLE_PROFILER
        if (bridge == nullptr) {
            const ProfilerReply reply = BuildProfilerUnavailableReply(false);
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        }
        const ParsedProfilerSet parsed =
            ParseProfilerSet(req.get_param_value("capture"), req.get_param_value("display_pause"), req.body);
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (parsed.capture) {
            bridge->SetCapture(*parsed.capture);
        }
        if (parsed.displayPause) {
            bridge->SetDisplayPaused(*parsed.displayPause);
        }
        res.set_content(BuildProfilerSetReplyJson(bridge->CaptureEnabled(), bridge->DisplayPaused()), "application/json");
#else
        const ProfilerReply reply = BuildProfilerUnavailableReply(bridge != nullptr);
        res.status = reply.status;
        res.set_content(reply.body, "application/json");
#endif
    });
}

} // namespace gte::Network
