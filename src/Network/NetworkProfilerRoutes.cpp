#include "NetworkProfilerRoutes.h"

#include "NetworkProfilerTimeline.h"
#include "NetworkRoutes.h"
#include "ViewWireName.h"

#include "../Application/ProfilerSnapshotBridge.h"
#include "../Core/GpuViewMetrics.h"
#include "../Profiling/ProfilerSnapshotBuilder.h"
#include "../Renderer/PresentPolicy.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <system_error>
#include <vector>

namespace gte::Network {

namespace {

using Profiling::Metric;
using Profiling::ProfilerSnapshot;
using Profiling::SampleStatus;

#if GTE_ENABLE_PROFILER
constexpr std::chrono::milliseconds kSnapshotWait{ 500 };
#endif

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
    for (const rg::ViewScope scope : kAllViewScopes) {
        out[ViewWireName(scope)] = MetricToJson(metrics[static_cast<std::size_t>(ToViewSlot(scope))]);
    }
    return out;
}

nlohmann::json Envelope(const ProfilerSnapshot& snapshot, const char* state = "ok")
{
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["schema_version"] = snapshot.schemaVersion;
    out["frame_index"] = snapshot.frameIndex;
    out["state"] = state;
    out["has_frame"] = snapshot.hasFrame;
    out["snapshot_age"] = MetricToJson(snapshot.snapshotAge);
    return out;
}

nlohmann::json LiveBlocksToJson(const ProfilerSnapshot& snapshot)
{
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
    return live;
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

constexpr double kMaxSpikeThresholdMs = 10000.0;

std::optional<float> ClampThreshold(double value)
{
    if (!std::isfinite(value) || value < 0.0 || value > kMaxSpikeThresholdMs) {
        return std::nullopt;
    }
    return static_cast<float>(value);
}

// Whole-string decimal number, finite and within 0..kMaxSpikeThresholdMs.
std::optional<float> ParseThresholdText(const std::string& text)
{
    if (text.empty() || std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size()) {
        return std::nullopt;
    }
    return ClampThreshold(value);
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
    device["present_policy"] = PresentPolicyName(snapshot.device.presentUncapped ? PresentPolicy::Uncapped : PresentPolicy::Capped);
    device["consecutive_frame_timeouts"] = MetricToJson(snapshot.device.consecutiveFrameTimeouts);
    device["abandoned_objects"] = MetricToJson(snapshot.device.abandonedObjects);
    out["device_state"] = std::move(device);

    out["live_block"] = LiveBlocksToJson(snapshot);

    const Profiling::SnapshotVerdictDetail& detail = snapshot.verdictDetail;
    nlohmann::json verdict = nlohmann::json::object();
    verdict["text"] = Text(snapshot.verdict);
    verdict["kind"] = Profiling::ToString(detail.kind);
    verdict["frames"] = MetricToJson(detail.frames);
    verdict["frame"] = MetricToJson(detail.frame);
    verdict["cpu_work"] = MetricToJson(detail.cpuWork);
    verdict["gpu_wait"] = MetricToJson(detail.gpuWait);
    verdict["present_wait"] = MetricToJson(detail.presentWait);
    verdict["other_wait"] = MetricToJson(detail.otherWait);
    verdict["untracked"] = MetricToJson(detail.untracked);
    verdict["stall_share"] = MetricToJson(detail.stallShare);
    verdict["stall_site"] = Text(detail.stallSite);
    out["verdict"] = std::move(verdict);

    nlohmann::json overhead = nlohmann::json::object();
    overhead["cost_per_event"] = MetricToJson(snapshot.overhead.costPerEvent);
    overhead["estimated"] = MetricToJson(snapshot.overhead.estimated);
    overhead["share"] = MetricToJson(snapshot.overhead.share);
    overhead["warn"] = snapshot.overhead.warn;
    out["overhead"] = std::move(overhead);

    nlohmann::json timeline = nlohmann::json::object();
    timeline["aligned"] = snapshot.timeline.aligned;
    timeline["reason"] = Text(snapshot.timeline.reason);
    out["timeline"] = std::move(timeline);

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
    out["frozen_total"] = MetricToJson(Profiling::PresentMetric(static_cast<double>(snapshot.spikeTotal), Profiling::Unit::Count));
    out["pinned"] = MetricToJson(Profiling::PresentMetric(static_cast<double>(snapshot.spikePinned), Profiling::Unit::Count));
    out["pin_limit"] = MetricToJson(Profiling::PresentMetric(static_cast<double>(Profiling::kMaxPinnedSpikes), Profiling::Unit::Count));

    nlohmann::json spikes = nlohmann::json::array();
    for (std::uint32_t i = 0; i < snapshot.spikeCount && i < snapshot.spikes.size(); ++i) {
        const Profiling::SnapshotSpikeRow& row = snapshot.spikes[i];
        nlohmann::json item = nlohmann::json::object();
        item["frame_index"] = row.frameIndex;
        item["cpu"] = MetricToJson(row.cpu);
        item["median"] = MetricToJson(row.median);
        item["excess"] = MetricToJson(row.excess);
        item["dominant_self"] = MetricToJson(row.dominantSelf);
        item["gpu_span"] = MetricToJson(row.gpuSpan);
        item["classification"] = Text(row.classification);
        item["cause"] = Text(row.cause);
        item["marker_present"] = row.markerPresent;
        item["pinned"] = row.pinned;
        item["gpu_attached"] = row.gpuAttached;
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

ProfilerReply BuildProfilerReply(ProfilerRoute route, const ProfilerSnapshot& snapshot, std::size_t frameCount)
{
    switch (route) {
    case ProfilerRoute::Overview: return { 200, ProfilerSnapshotToJson(snapshot).dump() };
    case ProfilerRoute::Frames: return { 200, ProfilerFramesToJson(snapshot, frameCount).dump() };
    case ProfilerRoute::Spikes: return { 200, ProfilerSpikesToJson(snapshot).dump() };
    }
    return { 500, BuildGenericErrorResponseJson("unknown profiler route") };
}

ProfilerReply BuildProfilerNoSnapshotReply(const ProfilerSnapshot& liveOnly)
{
    nlohmann::json out = Envelope(liveOnly, "no_snapshot");
    out["live_block"] = LiveBlocksToJson(liveOnly);
    return { 200, out.dump() };
}

ProfilerReply BuildProfilerUnavailableReply()
{
#if GTE_ENABLE_PROFILER
    return { 503, BuildGenericErrorResponseJson("profiler bridge not available") };
#else
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["schema_version"] = Profiling::kProfilerSchemaVersion;
    out["state"] = "compiled_out";
    return { 200, out.dump() };
#endif
}

ParsedProfilerSet ParseProfilerSet(const std::string& captureText, const std::string& displayPauseText,
    const std::string& body, const std::string& pinText, const std::string& unpinText,
    const std::string& spikeThresholdText)
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
    if (!pinText.empty()) {
        result.pin = ParseSpikeFrameIndex(pinText);
        if (!result.pin) {
            result.errorMessage = "invalid parameter: pin - must be the frame_index of a frozen spike";
            return result;
        }
    }
    if (!unpinText.empty()) {
        result.unpin = ParseSpikeFrameIndex(unpinText);
        if (!result.unpin) {
            result.errorMessage = "invalid parameter: unpin - must be the frame_index of a frozen spike";
            return result;
        }
    }
    if (!spikeThresholdText.empty()) {
        result.spikeThresholdMs = ParseThresholdText(spikeThresholdText);
        if (!result.spikeThresholdMs) {
            result.errorMessage = "invalid parameter: spike_threshold_ms - must be a number from 0 to 10000";
            return result;
        }
    }

    if (!body.empty()) {
        const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            result.errorMessage = "invalid body: expected a JSON object";
            return result;
        }
        const auto readBool = [&](const char* key, std::optional<bool>& target) {
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
        const auto readFrame = [&](const char* key, std::optional<std::uint64_t>& target) {
            const auto it = json.find(key);
            if (it == json.end()) {
                return true;
            }
            if (!it->is_number_unsigned()) {
                result.errorMessage = std::string("invalid body: ") + key + " must be a whole frame index";
                return false;
            }
            if (!target) {
                target = it->get<std::uint64_t>();
            }
            return true;
        };
        if (!readBool("capture", result.capture) || !readBool("display_pause", result.displayPause)
            || !readFrame("pin", result.pin) || !readFrame("unpin", result.unpin)) {
            return result;
        }
        const auto threshold = json.find("spike_threshold_ms");
        if (threshold != json.end() && !result.spikeThresholdMs) {
            if (!threshold->is_number()) {
                result.errorMessage = "invalid body: spike_threshold_ms must be a number";
                return result;
            }
            result.spikeThresholdMs = ClampThreshold(threshold->get<double>());
            if (!result.spikeThresholdMs) {
                result.errorMessage = "invalid body: spike_threshold_ms must be a number from 0 to 10000";
                return result;
            }
        }
    }

    if (!result.capture && !result.displayPause && !result.pin && !result.unpin && !result.spikeThresholdMs) {
        result.errorMessage = "nothing to set: pass capture, display_pause, pin, unpin and/or spike_threshold_ms";
        return result;
    }
    result.valid = true;
    return result;
}

std::string BuildProfilerSetReplyJson(const ProfilerSetState& state)
{
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["capture"] = state.capture;
    out["display_paused"] = state.displayPaused;
    out["pinned_spikes"] = MetricToJson(Profiling::PresentMetric(static_cast<double>(state.pinnedSpikes), Profiling::Unit::Count));
    out["spike_threshold"] = MetricToJson(Profiling::PresentMetric(state.spikeThresholdMs, Profiling::Unit::Milliseconds));
    return out.dump();
}

std::optional<std::uint64_t> ParseSpikeFrameIndex(const std::string& text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

nlohmann::json ProfilerSpikeDetailToJson(const Profiling::SpikeDetail& detail)
{
    nlohmann::json out = nlohmann::json::object();
    out["success"] = true;
    out["schema_version"] = Profiling::kProfilerSchemaVersion;
    out["frame_index"] = detail.frameIndex;
    out["state"] = "ok";
    out["pinned"] = detail.pinned;
    out["gpu_attached"] = detail.gpuAttached;
    out["classification"] = Profiling::ToString(detail.classification.kind);
    out["cause"] = detail.cause;
    out["marker_present"] = detail.classification.markerPresent;
    out["frame"] = MetricToJson(Profiling::PresentMetric(detail.frameMs, Profiling::Unit::Milliseconds));
    out["median"] = MetricToJson(Profiling::PresentMetric(detail.medianMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived));
    out["excess"] = MetricToJson(Profiling::PresentMetric(detail.classification.excessMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived));
    out["dominant_self"] = MetricToJson(
        Profiling::PresentMetric(detail.classification.dominantSelfMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived));
    out["gpu_span"] = MetricToJson(Profiling::MetricFromGpuView(detail.gpu.span));

    nlohmann::json views = nlohmann::json::object();
    for (const rg::ViewScope scope : kAllViewScopes) {
        views[ViewWireName(scope)] = MetricToJson(Profiling::MetricFromGpuView(detail.gpu.views[static_cast<std::size_t>(ToViewSlot(scope))]));
    }
    out["gpu_views"] = std::move(views);
    out["dropped_scopes"] = MetricToJson(Profiling::PresentMetric(static_cast<double>(detail.droppedScopes), Profiling::Unit::Count));

    nlohmann::json scopes = nlohmann::json::array();
    for (const Profiling::SpikeScopeRow& row : detail.scopes) {
        nlohmann::json item = nlohmann::json::object();
        item["name"] = row.name;
        item["kind"] = row.kind == Profiling::ScopeKind::Blocked ? "blocked"
            : row.kind == Profiling::ScopeKind::Marker           ? "marker"
                                                                 : "work";
        item["depth"] = row.depth;
        item["start"] = MetricToJson(Profiling::PresentMetric(row.startMs, Profiling::Unit::Milliseconds));
        item["duration"] = MetricToJson(Profiling::PresentMetric(row.durationMs, Profiling::Unit::Milliseconds));
        item["self"] = MetricToJson(Profiling::PresentMetric(row.selfMs, Profiling::Unit::Milliseconds, Profiling::MetricKind::Derived));
        scopes.push_back(std::move(item));
    }
    out["scopes"] = std::move(scopes);
    return out;
}

ProfilerReply BuildProfilerSpikeReply(const std::optional<Profiling::SpikeDetail>& detail, std::uint64_t frameIndex)
{
    if (!detail) {
        return { 404, BuildGenericErrorResponseJson("no frozen spike for frame " + std::to_string(frameIndex)
                          + " - it was never frozen or it was evicted") };
    }
    return { 200, ProfilerSpikeDetailToJson(*detail).dump() };
}

ProfilerReply BuildProfilerPinErrorReply(bool limitReached, std::uint64_t frameIndex, std::size_t pinned)
{
    if (limitReached) {
        return { 409, BuildGenericErrorResponseJson("pin limit reached: " + std::to_string(pinned) + " of "
                          + std::to_string(Profiling::kMaxPinnedSpikes) + " spikes are pinned - unpin one first") };
    }
    return { 404, BuildGenericErrorResponseJson("no frozen spike for frame " + std::to_string(frameIndex)
                      + " - it was never frozen or it was evicted") };
}

nlohmann::json ProfilerExportToJson(
    const ProfilerSnapshot& snapshot, std::size_t count, std::span<const Profiling::SpikeDetail> pinned)
{
    nlohmann::json out = ProfilerFramesToJson(snapshot, count);
    out["overview"] = ProfilerSnapshotToJson(snapshot);
    nlohmann::json spikes = nlohmann::json::array();
    for (const Profiling::SpikeDetail& detail : pinned) {
        spikes.push_back(ProfilerSpikeDetailToJson(detail));
    }
    out["pinned_spikes"] = std::move(spikes);
    return out;
}

ProfilerImageReply BuildProfilerTimelineReply(
    const std::optional<Profiling::SpikeDetail>& frame, bool namedFrame, std::uint64_t frameIndex, int width)
{
    if (!frame) {
        if (namedFrame) {
            return { 404, {}, BuildGenericErrorResponseJson("no frozen spike for frame " + std::to_string(frameIndex)
                                  + " - it was never frozen or it was evicted") };
        }
        return { 503, {}, BuildGenericErrorResponseJson("no profiler frame published yet - the main thread has not published one, try again") };
    }
    try {
        const Profiling::TimelineLayout layout = Profiling::BuildTimelineLayout(*frame);
        const std::vector<std::uint8_t> png = RenderTimelinePng(PlanTimelinePixels(layout, width));
        return { 200, "image/png", std::string(reinterpret_cast<const char*>(png.data()), png.size()) };
    } catch (const std::exception& error) {
        return { 500, {}, BuildGenericErrorResponseJson(std::string("timeline image failed: ") + error.what()) };
    }
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
                reply = BuildProfilerUnavailableReply();
            } else {
                const std::optional<ProfilerSnapshot> latest = bridge->Latest(kSnapshotWait);
                reply = latest ? BuildProfilerReply(route, *latest, frameCount)
                               : BuildProfilerNoSnapshotReply(bridge->LiveOnlySnapshot());
            }
#else
            static_cast<void>(req);
            static_cast<void>(route);
            static_cast<void>(bridge);
            reply = BuildProfilerUnavailableReply();
#endif
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
        };
    };

    server.Get("/profiler", serve(ProfilerRoute::Overview));
    server.Get("/profiler/frames", serve(ProfilerRoute::Frames));
    server.Get("/profiler/spikes", serve(ProfilerRoute::Spikes));

    server.Get("/profiler/spike", [bridge](const httplib::Request& req, httplib::Response& res) {
        ProfilerReply reply;
#if GTE_ENABLE_PROFILER
        if (bridge == nullptr) {
            reply = BuildProfilerUnavailableReply();
        } else {
            const std::optional<std::uint64_t> index = ParseSpikeFrameIndex(req.get_param_value("index"));
            if (!index) {
                reply = { 400, BuildGenericErrorResponseJson("invalid query parameter: index - the frame_index of a frozen spike") };
            } else {
                reply = BuildProfilerSpikeReply(bridge->SpikeFrame(*index), *index);
            }
        }
#else
        static_cast<void>(req);
        static_cast<void>(bridge);
        reply = BuildProfilerUnavailableReply();
#endif
        res.status = reply.status;
        res.set_content(reply.body, "application/json");
    });

    server.Post("/profiler/set", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser != nullptr && refuseBrowser(req, res)) {
            return;
        }
#if GTE_ENABLE_PROFILER
        if (bridge == nullptr) {
            const ProfilerReply reply = BuildProfilerUnavailableReply();
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        }
        const ParsedProfilerSet parsed = ParseProfilerSet(req.get_param_value("capture"), req.get_param_value("display_pause"),
            req.body, req.get_param_value("pin"), req.get_param_value("unpin"), req.get_param_value("spike_threshold_ms"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }

        // Spike changes first: a refused pin leaves the switches untouched.
        if (parsed.unpin && !bridge->UnpinSpike(*parsed.unpin)) {
            const ProfilerReply reply = BuildProfilerPinErrorReply(false, *parsed.unpin, bridge->PinnedSpikes());
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        }
        if (parsed.pin) {
            const Profiling::PinResult pinned = bridge->PinSpike(*parsed.pin);
            if (pinned != Profiling::PinResult::Ok) {
                const ProfilerReply reply = BuildProfilerPinErrorReply(
                    pinned == Profiling::PinResult::LimitReached, *parsed.pin, bridge->PinnedSpikes());
                res.status = reply.status;
                res.set_content(reply.body, "application/json");
                return;
            }
        }
        if (parsed.capture) {
            bridge->SetCapture(*parsed.capture);
        }
        if (parsed.displayPause) {
            bridge->SetDisplayPaused(*parsed.displayPause);
        }
        if (parsed.spikeThresholdMs) {
            bridge->SetSpikeThresholdMs(*parsed.spikeThresholdMs);
        }

        ProfilerSetState state;
        state.capture = bridge->CaptureEnabled();
        state.displayPaused = bridge->DisplayPaused();
        state.pinnedSpikes = bridge->PinnedSpikes();
        state.spikeThresholdMs = bridge->SpikeThresholdMs();
        res.set_content(BuildProfilerSetReplyJson(state), "application/json");
#else
        static_cast<void>(bridge);
        const ProfilerReply reply = BuildProfilerUnavailableReply();
        res.status = reply.status;
        res.set_content(reply.body, "application/json");
#endif
    });

    server.Get("/profiler/timeline_image", [bridge](const httplib::Request& req, httplib::Response& res) {
#if GTE_ENABLE_PROFILER
        if (bridge == nullptr) {
            const ProfilerReply reply = BuildProfilerUnavailableReply();
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        }
        const std::optional<int> width = ParseTimelineWidth(req.get_param_value("width"));
        if (!width) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson("invalid query parameter: width - a whole number from "
                                + std::to_string(kTimelineWidthMin) + " to " + std::to_string(kTimelineWidthMax)),
                "application/json");
            return;
        }

        ProfilerImageReply reply;
        const std::string frameText = req.get_param_value("frame");
        if (frameText.empty()) {
            // Asking also tells the main thread to keep publishing frames.
            static_cast<void>(bridge->Latest(kSnapshotWait));
            reply = BuildProfilerTimelineReply(bridge->LatestFrameDetail(), false, 0, *width);
        } else {
            const std::optional<std::uint64_t> index = ParseSpikeFrameIndex(frameText);
            if (!index) {
                res.status = 400;
                res.set_content(BuildGenericErrorResponseJson("invalid query parameter: frame - the frame_index of a frozen spike"),
                    "application/json");
                return;
            }
            reply = BuildProfilerTimelineReply(bridge->SpikeFrame(*index), true, *index, *width);
        }
        res.status = reply.status;
        if (reply.contentType.empty()) {
            res.set_content(reply.body, "application/json");
        } else {
            res.set_content(reply.body.data(), reply.body.size(), reply.contentType);
        }
#else
        static_cast<void>(req);
        static_cast<void>(bridge);
        const ProfilerReply reply = BuildProfilerUnavailableReply();
        res.status = reply.status;
        res.set_content(reply.body, "application/json");
#endif
    });

    server.Post("/profiler/export", [bridge, refuseBrowser](const httplib::Request& req, httplib::Response& res) {
        if (refuseBrowser != nullptr && refuseBrowser(req, res)) {
            return;
        }
#if GTE_ENABLE_PROFILER
        if (bridge == nullptr) {
            const ProfilerReply reply = BuildProfilerUnavailableReply();
            res.status = reply.status;
            res.set_content(reply.body, "application/json");
            return;
        }
        const std::optional<std::size_t> count = ParseProfilerFrameCount(req.get_param_value("frames"));
        if (!count) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson("invalid query parameter: frames - must be a whole number"), "application/json");
            return;
        }
        const std::optional<ProfilerSnapshot> snapshot = bridge->Latest(kSnapshotWait);
        ProfilerReply reply;
        if (!snapshot) {
            reply = { 503, BuildGenericErrorResponseJson("no profiler snapshot yet - the main thread has not published one, try again") };
        } else {
            std::vector<Profiling::SpikeDetail> pinned;
            for (std::uint32_t i = 0; i < snapshot->spikeCount && i < snapshot->spikes.size(); ++i) {
                if (snapshot->spikes[i].pinned) {
                    if (std::optional<Profiling::SpikeDetail> detail = bridge->SpikeFrame(snapshot->spikes[i].frameIndex)) {
                        pinned.push_back(std::move(*detail));
                    }
                }
            }
            reply = { 200, ProfilerExportToJson(*snapshot, *count, pinned).dump() };
        }
        res.status = reply.status;
        res.set_content(reply.body, "application/json");
#else
        static_cast<void>(bridge);
        const ProfilerReply reply = BuildProfilerUnavailableReply();
        res.status = reply.status;
        res.set_content(reply.body, "application/json");
#endif
    });
}

} // namespace gte::Network
