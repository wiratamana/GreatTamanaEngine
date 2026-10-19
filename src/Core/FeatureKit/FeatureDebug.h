#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace gte::fx {

using FeatureDebugValue = std::variant<bool, std::int64_t, double, std::string>;

struct FeatureDebugField {
    std::string key;
    FeatureDebugValue value;
};

// One key=value pair of a debug request; the value is still text.
struct FeatureDebugArg {
    std::string key;
    std::string value;
};

struct FeatureDebugResult {
    bool ok = true;
    std::string message;
    std::string badKey; // The key that made the request fail, if any.
    std::vector<FeatureDebugField> fields;
};

// Text-driven inspection and tuning hook of a feature. Called on the main thread only.
class IFeatureDebugEndpoint {
public:
    virtual ~IFeatureDebugEndpoint() = default;

    virtual FeatureDebugResult GetState() const = 0;

    // All arguments are applied or none. On success `fields` holds the new state; a failure names the key in badKey.
    virtual FeatureDebugResult Set(std::span<const FeatureDebugArg> args) = 0;

    virtual FeatureDebugResult Invoke(std::string_view action, std::span<const FeatureDebugArg> args) = 0;
};

// Whole-value parsers: no trailing text, no sign surprises, no nan or inf.
inline std::optional<bool> ParseDebugBool(std::string_view text) noexcept
{
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return std::nullopt;
}

inline std::optional<std::uint64_t> ParseDebugUnsigned(std::string_view text) noexcept
{
    if (text.empty() || text.front() == '+' || text.front() == '-') {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

inline std::optional<double> ParseDebugFloat(std::string_view text) noexcept
{
    if (text.starts_with('+')) {
        return std::nullopt;
    }
    double value = 0.0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace gte::fx
