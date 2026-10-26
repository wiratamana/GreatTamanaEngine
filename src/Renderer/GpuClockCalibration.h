#pragma once

// Maps GPU timestamp ticks onto the CPU clock from paired samples. Pure math, no Vulkan.

#include <cstdint>
#include <optional>
#include <span>

namespace gte {

// One reading of both clocks taken together.
struct ClockSample {
    std::uint64_t gpuTicks = 0;
    std::uint64_t cpuNs = 0;
};

enum class ClockCalibrationState : std::uint8_t { Present, Unsupported, Error };

enum class ClockCalibrationReason : std::uint8_t {
    None,
    ExtensionNotEnabled,
    TooFewSamples,
    ClockNotMonotonic,
    BadPeriod,
    SlopeOutOfRange,
    UncertaintyTooLarge,
};

// Fit result: cpuNs = origin.cpuNs + offsetNs + slope * (ticks - origin.gpuTicks) * periodNs.
struct ClockCalibration {
    ClockCalibrationState state = ClockCalibrationState::Unsupported;
    ClockCalibrationReason reason = ClockCalibrationReason::ExtensionNotEnabled;
    ClockSample origin{};
    double periodNs = 0.0;      // GPU ns per tick
    double slope = 1.0;         // CPU ns per GPU ns (drift)
    double offsetNs = 0.0;
    double uncertaintyNs = 0.0; // largest residual of the fit
};

// Samples must be strictly increasing in both clocks. The fit is rejected when it is not trustworthy.
inline constexpr double kMaxClockSlopeError = 0.1;
inline constexpr double kMaxClockUncertaintyNs = 1.0e6;

[[nodiscard]] ClockCalibration FitClockCalibration(std::span<const ClockSample> samples, double periodNs) noexcept;

// A calibration that carries only the reason it is missing.
[[nodiscard]] ClockCalibration MissingClockCalibration(ClockCalibrationReason reason) noexcept;

// CPU nanoseconds for a GPU tick, or nothing unless the calibration is Present.
[[nodiscard]] std::optional<std::int64_t> GpuTicksToCpuNs(const ClockCalibration& calibration, std::uint64_t gpuTicks) noexcept;

[[nodiscard]] const char* ToString(ClockCalibrationReason reason) noexcept;

} // namespace gte
