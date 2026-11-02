#include "PhysicsDebugSettings.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace gte {

namespace {

enum class Kind { Float, Int, Bool, Enum };

// One settings key. Every value is carried as a double (bool = 0/1, enum = index into `names`).
struct Row {
    std::string_view key;
    Kind kind;
    double min;
    double max;
    double legacy;
    double (*get)(const GlobalPhysicsSettings&);
    void (*set)(GlobalPhysicsSettings&, double);
    std::array<std::string_view, 2> names; // Enum only.
};

// Bool and Enum rows ignore min/max.
constexpr double kNoRange = 0.0;

const std::array<Row, 14>& Table()
{
    static const std::array<Row, 14> rows = { {
        { "gravity_x", Kind::Float, -1000.0, 1000.0, 0.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.gravity.x); },
            [](GlobalPhysicsSettings& s, double v) { s.gravity.x = static_cast<float>(v); }, {} },
        { "gravity_y", Kind::Float, -1000.0, 1000.0, -9.8,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.gravity.y); },
            [](GlobalPhysicsSettings& s, double v) { s.gravity.y = static_cast<float>(v); }, {} },
        { "gravity_z", Kind::Float, -1000.0, 1000.0, 0.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.gravity.z); },
            [](GlobalPhysicsSettings& s, double v) { s.gravity.z = static_cast<float>(v); }, {} },
        { "fixed_step_hz", Kind::Float, 30.0, 480.0, 60.0,
            [](const GlobalPhysicsSettings& s) { return 1.0 / static_cast<double>(s.fixedTimestepSeconds); },
            [](GlobalPhysicsSettings& s, double v) { s.fixedTimestepSeconds = static_cast<float>(1.0 / v); }, {} },
        { "max_steps_per_frame", Kind::Int, 1.0, 32.0, 4.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.maxStepsPerFrame); },
            [](GlobalPhysicsSettings& s, double v) { s.maxStepsPerFrame = static_cast<int>(v); }, {} },
        { "collision_enabled", Kind::Bool, kNoRange, kNoRange, 1.0,
            [](const GlobalPhysicsSettings& s) { return s.collisionEnabled ? 1.0 : 0.0; },
            [](GlobalPhysicsSettings& s, double v) { s.collisionEnabled = v != 0.0; }, {} },
        { "damping_multiplier", Kind::Float, 0.0, 50.0, 1.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.dampingMultiplier); },
            [](GlobalPhysicsSettings& s, double v) { s.dampingMultiplier = static_cast<float>(v); }, {} },
        { "stiffness_multiplier", Kind::Float, 0.0, 50.0, 1.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.stiffnessMultiplier); },
            [](GlobalPhysicsSettings& s, double v) { s.stiffnessMultiplier = static_cast<float>(v); }, {} },
        { "damping_mode", Kind::Enum, kNoRange, kNoRange, 1.0,
            [](const GlobalPhysicsSettings& s) { return s.useRawPmxDamping ? 1.0 : 0.0; },
            [](GlobalPhysicsSettings& s, double v) { s.useRawPmxDamping = v != 0.0; }, { "mapped", "raw_pmx" } },
        { "collision_fit_enabled", Kind::Bool, kNoRange, kNoRange, 0.0,
            [](const GlobalPhysicsSettings& s) { return s.useCollisionFit ? 1.0 : 0.0; },
            [](GlobalPhysicsSettings& s, double v) { s.useCollisionFit = v != 0.0; }, {} },
        { "bend_limits_enabled", Kind::Bool, kNoRange, kNoRange, 0.0,
            [](const GlobalPhysicsSettings& s) { return s.bendLimitsEnabled ? 1.0 : 0.0; },
            [](GlobalPhysicsSettings& s, double v) { s.bendLimitsEnabled = v != 0.0; }, {} },
        { "bend_limit_scale", Kind::Float, 0.0, 20.0, 1.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.bendLimitScale); },
            [](GlobalPhysicsSettings& s, double v) { s.bendLimitScale = static_cast<float>(v); }, {} },
        { "bend_limit_min_degrees", Kind::Float, 0.0, 180.0, 0.0,
            [](const GlobalPhysicsSettings& s) { return static_cast<double>(s.bendLimitMinDegrees); },
            [](GlobalPhysicsSettings& s, double v) { s.bendLimitMinDegrees = static_cast<float>(v); }, {} },
        { "interpolate_output", Kind::Bool, kNoRange, kNoRange, 0.0,
            [](const GlobalPhysicsSettings& s) { return s.interpolateOutput ? 1.0 : 0.0; },
            [](GlobalPhysicsSettings& s, double v) { s.interpolateOutput = v != 0.0; }, {} },
    } };
    return rows;
}

const Row* FindRow(std::string_view key)
{
    for (const Row& row : Table()) {
        if (row.key == key) {
            return &row;
        }
    }
    return nullptr;
}

std::string FormatNumber(double value)
{
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.7g", value);
    return buffer;
}

std::string FormatValue(const Row& row, double value)
{
    switch (row.kind) {
    case Kind::Bool: return value != 0.0 ? "true" : "false";
    case Kind::Enum: return std::string(row.names[value != 0.0 ? 1 : 0]);
    case Kind::Int: return std::to_string(static_cast<long long>(std::llround(value)));
    case Kind::Float: break;
    }
    return FormatNumber(value);
}

// Parses a plain decimal number: digits, '-', '.', 'e', 'E' only. No '+', spaces, hex, nan or inf.
bool ParseNumber(std::string_view text, bool wholeOnly, double& out)
{
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        const bool digit = c >= '0' && c <= '9';
        const bool floatChar = c == '-' || c == '.' || c == 'e' || c == 'E';
        if (!digit && !(floatChar && !wholeOnly) && !(c == '-' && wholeOnly)) {
            return false;
        }
    }
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
        return false;
    }
    if (wholeOnly && value != std::floor(value)) {
        return false;
    }
    out = value;
    return true;
}

bool ParseValue(const Row& row, std::string_view text, double& out, std::string& error)
{
    switch (row.kind) {
    case Kind::Bool: {
        bool flag = false;
        if (!ParsePhysicsBool(text, flag)) {
            error = std::string(row.key) + " must be true or false";
            return false;
        }
        out = flag ? 1.0 : 0.0;
        return true;
    }
    case Kind::Enum:
        for (std::size_t i = 0; i < row.names.size(); ++i) {
            if (text == row.names[i]) {
                out = static_cast<double>(i);
                return true;
            }
        }
        error = std::string(row.key) + " must be " + std::string(row.names[0]) + " or " + std::string(row.names[1]);
        return false;
    case Kind::Int:
    case Kind::Float: {
        if (!ParseNumber(text, row.kind == Kind::Int, out)) {
            error = std::string(row.key) + " must be a plain "
                + (row.kind == Kind::Int ? "whole number" : "finite number");
            return false;
        }
        if (out < row.min || out > row.max) {
            error = std::string(row.key) + " must be between " + FormatNumber(row.min) + " and " + FormatNumber(row.max);
            return false;
        }
        return true;
    }
    }
    return false;
}

} // namespace

bool ParsePhysicsBool(std::string_view text, bool& out) noexcept
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

bool ApplyPhysicsSettingPairs(GlobalPhysicsSettings& settings, const PhysicsSettingPairs& pairs,
    std::string& errorMessage, std::string& errorKey)
{
    struct Parsed {
        const Row* row;
        double value;
    };
    std::vector<Parsed> parsed;
    parsed.reserve(pairs.size());

    for (const auto& [key, text] : pairs) {
        const Row* row = FindRow(key);
        if (row == nullptr) {
            errorKey = key;
            errorMessage = "unknown key '" + key + "'";
            return false;
        }
        for (const Parsed& earlier : parsed) {
            if (earlier.row == row) {
                errorKey = key;
                errorMessage = "duplicate key '" + key + "'";
                return false;
            }
        }
        double value = 0.0;
        std::string error;
        if (!ParseValue(*row, text, value, error)) {
            errorKey = key;
            errorMessage = std::move(error);
            return false;
        }
        parsed.push_back({ row, value });
    }

    for (const Parsed& entry : parsed) {
        entry.row->set(settings, entry.value);
    }
    return true;
}

bool ApplyPhysicsPreset(GlobalPhysicsSettings& settings, std::string_view presetName)
{
    if (presetName == "default") {
        const WindSettings wind = settings.wind; // not part of any preset.
        settings = GlobalPhysicsSettings{};
        settings.wind = wind;
        return true;
    }
    if (presetName == "legacy") {
        for (const Row& row : Table()) {
            row.set(settings, row.legacy);
        }
        return true;
    }
    return false;
}

PhysicsSettingPairs DescribeSettings(const GlobalPhysicsSettings& settings)
{
    PhysicsSettingPairs result;
    result.reserve(Table().size());
    for (const Row& row : Table()) {
        result.emplace_back(std::string(row.key), FormatValue(row, row.get(settings)));
    }
    return result;
}

bool IsPhysicsEntitySettingKey(std::string_view key)
{
    return key == "rig_enabled" || key == "rig_frozen";
}

} // namespace gte
