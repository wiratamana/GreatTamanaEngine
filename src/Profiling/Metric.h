#pragma once

// A number with its unit, origin and trust level. Output code prints metrics only through FormatMetric().

#include "ProfilingTypes.h"
#include "ScopeEvent.h"

#include <cstdint>
#include <string>

namespace gte::Profiling {

enum class Unit : std::uint8_t { Milliseconds, Count, Bytes, Percent };

enum class MetricKind : std::uint8_t {
    Measured,  // read from a clock, a query or a counter
    Derived,   // computed from measured values
    Estimated, // a model, not a reading
};

// Why a metric is not a plain Present value.
enum class Reason : std::uint8_t {
    None,
    CaptureOff,
    NoTarget,
    Folded,
    NotReady,
    DeviceUnsupported,
    CompiledOut,
    Dropped,
    Other,
};

struct Metric {
    double value = 0.0; // meaningful for Present and Partial only
    Unit unit = Unit::Milliseconds;
    MetricKind kind = MetricKind::Measured;
    SampleStatus status = SampleStatus::Absent;
    std::uint32_t ageFrames = 0; // frames since the value was taken
    Reason reason = Reason::None;
};

constexpr Metric PresentMetric(double value, Unit unit, MetricKind kind = MetricKind::Measured) noexcept
{
    return Metric{ value, unit, kind, SampleStatus::Present, 0, Reason::None };
}

constexpr Metric MissingMetric(Unit unit, SampleStatus status, Reason reason = Reason::None) noexcept
{
    return Metric{ 0.0, unit, MetricKind::Measured, status, 0, reason };
}

// GPU view metric -> Metric. A folded pass count marks the value Partial with reason Folded.
Metric MetricFromGpuView(const GpuViewMetric& gpu) noexcept;

const char* ToString(Unit unit) noexcept;
const char* ToString(MetricKind kind) noexcept;
const char* ToString(Reason reason) noexcept;
const char* ToString(SampleStatus status) noexcept;

// "12.34 ms" for Present, "12.34 ms partial (reason)" for Partial, the status word otherwise. Never "N/A".
std::string FormatMetric(const Metric& metric);

} // namespace gte::Profiling
