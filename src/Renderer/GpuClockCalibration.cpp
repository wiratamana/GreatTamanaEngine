#include "GpuClockCalibration.h"

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

ClockCalibration Failed(ClockCalibrationReason reason) noexcept
{
    ClockCalibration out;
    out.state = ClockCalibrationState::Error;
    out.reason = reason;
    return out;
}

} // namespace

ClockCalibration MissingClockCalibration(ClockCalibrationReason reason) noexcept
{
    ClockCalibration out;
    out.state = ClockCalibrationState::Unsupported;
    out.reason = reason;
    return out;
}

ClockCalibration FitClockCalibration(std::span<const ClockSample> samples, double periodNs) noexcept
{
    if (!(periodNs > 0.0) || !std::isfinite(periodNs)) {
        return Failed(ClockCalibrationReason::BadPeriod);
    }
    if (samples.size() < 2) {
        return Failed(ClockCalibrationReason::TooFewSamples);
    }
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].gpuTicks <= samples[i - 1].gpuTicks || samples[i].cpuNs <= samples[i - 1].cpuNs) {
            return Failed(ClockCalibrationReason::ClockNotMonotonic);
        }
    }

    // Centre on the first sample so large absolute clock values keep their precision.
    const ClockSample origin = samples.front();
    const double count = static_cast<double>(samples.size());
    double sumX = 0.0;
    double sumY = 0.0;
    for (const ClockSample& s : samples) {
        sumX += static_cast<double>(s.gpuTicks - origin.gpuTicks) * periodNs;
        sumY += static_cast<double>(s.cpuNs - origin.cpuNs);
    }
    const double meanX = sumX / count;
    const double meanY = sumY / count;

    double sxx = 0.0;
    double sxy = 0.0;
    for (const ClockSample& s : samples) {
        const double dx = static_cast<double>(s.gpuTicks - origin.gpuTicks) * periodNs - meanX;
        const double dy = static_cast<double>(s.cpuNs - origin.cpuNs) - meanY;
        sxx += dx * dx;
        sxy += dx * dy;
    }
    if (!(sxx > 0.0)) {
        return Failed(ClockCalibrationReason::ClockNotMonotonic);
    }

    ClockCalibration out;
    out.origin = origin;
    out.periodNs = periodNs;
    out.slope = sxy / sxx;
    out.offsetNs = meanY - out.slope * meanX;
    if (!std::isfinite(out.slope) || std::fabs(out.slope - 1.0) > kMaxClockSlopeError) {
        return Failed(ClockCalibrationReason::SlopeOutOfRange);
    }

    double worst = 0.0;
    for (const ClockSample& s : samples) {
        const double x = static_cast<double>(s.gpuTicks - origin.gpuTicks) * periodNs;
        const double y = static_cast<double>(s.cpuNs - origin.cpuNs);
        worst = std::max(worst, std::fabs(y - (out.offsetNs + out.slope * x)));
    }
    out.uncertaintyNs = worst;
    if (worst > kMaxClockUncertaintyNs) {
        return Failed(ClockCalibrationReason::UncertaintyTooLarge);
    }

    out.state = ClockCalibrationState::Present;
    out.reason = ClockCalibrationReason::None;
    return out;
}

std::optional<std::int64_t> GpuTicksToCpuNs(const ClockCalibration& calibration, std::uint64_t gpuTicks) noexcept
{
    if (calibration.state != ClockCalibrationState::Present) {
        return std::nullopt;
    }
    const auto delta = static_cast<std::int64_t>(gpuTicks - calibration.origin.gpuTicks);
    const double offset = calibration.offsetNs + calibration.slope * static_cast<double>(delta) * calibration.periodNs;
    return static_cast<std::int64_t>(calibration.origin.cpuNs) + static_cast<std::int64_t>(std::llround(offset));
}

const char* ToString(ClockCalibrationReason reason) noexcept
{
    switch (reason) {
    case ClockCalibrationReason::None: return "none";
    case ClockCalibrationReason::ExtensionNotEnabled: return "calibrated timestamp extension not enabled";
    case ClockCalibrationReason::TooFewSamples: return "too few clock samples";
    case ClockCalibrationReason::ClockNotMonotonic: return "clock samples not increasing";
    case ClockCalibrationReason::BadPeriod: return "bad timestamp period";
    case ClockCalibrationReason::SlopeOutOfRange: return "clock drift out of range";
    case ClockCalibrationReason::UncertaintyTooLarge: return "clock fit too noisy";
    }
    return "unknown";
}

} // namespace gte
