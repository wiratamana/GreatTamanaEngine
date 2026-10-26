#include "Metric.h"

#include <cstdio>

namespace gte::Profiling {

namespace {

std::string FormatValue(double value, Unit unit)
{
    char text[48];
    switch (unit) {
    case Unit::Milliseconds: std::snprintf(text, sizeof(text), "%.2f ms", value); break;
    case Unit::Count: std::snprintf(text, sizeof(text), "%.0f", value); break;
    case Unit::Bytes: std::snprintf(text, sizeof(text), "%.1f MiB", value / (1024.0 * 1024.0)); break;
    case Unit::Percent: std::snprintf(text, sizeof(text), "%.1f %%", value); break;
    }
    return text;
}

} // namespace

Metric MetricFromGpuView(const GpuViewMetric& gpu) noexcept
{
    Metric metric;
    metric.value = static_cast<double>(gpu.milliseconds);
    metric.unit = Unit::Milliseconds;
    metric.kind = MetricKind::Measured;
    metric.status = gpu.status;
    if (gpu.foldedPasses != 0) {
        metric.reason = Reason::Folded;
    } else if (gpu.status == SampleStatus::Inactive) {
        metric.reason = Reason::NoTarget;
    } else if (gpu.status == SampleStatus::Unsupported) {
        metric.reason = Reason::DeviceUnsupported;
    }
    return metric;
}

const char* ToString(Unit unit) noexcept
{
    switch (unit) {
    case Unit::Milliseconds: return "ms";
    case Unit::Count: return "count";
    case Unit::Bytes: return "bytes";
    case Unit::Percent: return "percent";
    }
    return "unknown";
}

const char* ToString(MetricKind kind) noexcept
{
    switch (kind) {
    case MetricKind::Measured: return "measured";
    case MetricKind::Derived: return "derived";
    case MetricKind::Estimated: return "estimated";
    }
    return "unknown";
}

const char* ToString(Reason reason) noexcept
{
    switch (reason) {
    case Reason::None: return "none";
    case Reason::CaptureOff: return "capture_off";
    case Reason::NoTarget: return "no_target";
    case Reason::Folded: return "folded";
    case Reason::NotReady: return "not_ready";
    case Reason::DeviceUnsupported: return "device_unsupported";
    case Reason::CompiledOut: return "compiled_out";
    case Reason::Dropped: return "dropped";
    case Reason::Other: return "other";
    }
    return "unknown";
}

const char* ToString(SampleStatus status) noexcept
{
    switch (status) {
    case SampleStatus::Present: return "Present";
    case SampleStatus::Stale: return "Stale";
    case SampleStatus::Absent: return "Absent";
    case SampleStatus::Unsupported: return "Unsupported";
    case SampleStatus::Partial: return "Partial";
    case SampleStatus::Error: return "Error";
    case SampleStatus::Inactive: return "Inactive";
    }
    return "Unknown";
}

std::string FormatMetric(const Metric& metric)
{
    switch (metric.status) {
    case SampleStatus::Present:
        return FormatValue(metric.value, metric.unit);
    case SampleStatus::Partial:
    case SampleStatus::Stale: {
        std::string text = FormatValue(metric.value, metric.unit);
        text += metric.status == SampleStatus::Partial ? " partial" : " stale";
        if (metric.reason != Reason::None) {
            text += " (";
            text += ToString(metric.reason);
            text += ")";
        }
        return text;
    }
    default: {
        std::string text = ToString(metric.status);
        if (metric.reason != Reason::None) {
            text += " (";
            text += ToString(metric.reason);
            text += ")";
        }
        return text;
    }
    }
}

} // namespace gte::Profiling
