#include "BloomDebugEndpoint.h"

#include "BloomFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace gte {

namespace {

struct SettingKey {
    std::string_view name;
    float BloomSettings::* member;
};

// Every settable float key; enabled and levels have their own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "threshold", &BloomSettings::threshold },
    SettingKey{ "knee", &BloomSettings::knee },
    SettingKey{ "intensity", &BloomSettings::intensity },
};

constexpr std::string_view kEnabledKey = "enabled";
constexpr std::string_view kLevelsKey = "levels";

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

std::string_view StatusName(BloomFrameStatus status) noexcept
{
    switch (status) {
    case BloomFrameStatus::NotRendered: return "NotRendered";
    case BloomFrameStatus::Ok: return "Ok";
    case BloomFrameStatus::Disabled: return "Disabled";
    case BloomFrameStatus::PassDisabled: return "PassDisabled";
    case BloomFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case BloomFrameStatus::TargetFailed: return "TargetFailed";
    case BloomFrameStatus::ExtentTooSmall: return "ExtentTooSmall";
    }
    return "NotRendered";
}

// A float shown with 6 significant digits, so 0.3f reads 0.3 and not 0.3000000119.
double Shown(float value) noexcept
{
    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), value, std::chars_format::general, 6);
    double shown = value;
    if (written.ec == std::errc{}) {
        std::from_chars(text, written.ptr, shown);
    }
    return shown;
}

std::int64_t Count(std::uint64_t value) noexcept
{
    return static_cast<std::int64_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::int64_t>::max()));
}

void Add(fx::FeatureDebugResult& out, std::string key, fx::FeatureDebugValue value)
{
    out.fields.push_back({ std::move(key), std::move(value) });
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

} // namespace

fx::FeatureDebugResult BloomDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    const BloomSettings& s = m_feature.Settings();
    Add(out, std::string(kEnabledKey), s.enabled);
    for (const SettingKey& key : kSettingKeys) {
        Add(out, std::string(key.name), Shown(s.*key.member));
    }
    Add(out, std::string(kLevelsKey), static_cast<std::int64_t>(s.levels));

    for (const ScopeName& scope : kScopes) {
        const BloomViewSnapshot snapshot = m_feature.Snapshot(scope.scope);
        Add(out, std::string(scope.prefix).append("status"), std::string(StatusName(snapshot.status)));
        Add(out, std::string(scope.prefix).append("frameIndex"), Count(snapshot.frameIndex));
        Add(out, std::string(scope.prefix).append("activeLevels"), static_cast<std::int64_t>(snapshot.activeLevels));
    }
    return out;
}

fx::FeatureDebugResult BloomDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    BloomSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kEnabledKey) {
            const auto parsed = fx::ParseDebugBool(arg.value);
            if (!parsed) {
                return Fail(arg.key, "enabled must be true, false, 1 or 0");
            }
            candidate.enabled = *parsed;
            continue;
        }
        if (arg.key == kLevelsKey) {
            const auto parsed = fx::ParseDebugUnsigned(arg.value);
            if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
                return Fail(arg.key, "levels must be a whole number");
            }
            candidate.levels = static_cast<std::uint32_t>(*parsed);
            continue;
        }
        const auto found = std::find_if(kSettingKeys.begin(), kSettingKeys.end(),
            [&](const SettingKey& key) { return key.name == arg.key; });
        if (found == kSettingKeys.end()) {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.");
            return Fail(arg.key, readOnly ? "key is read-only" : "unknown key");
        }
        const auto parsed = fx::ParseDebugFloat(arg.value);
        if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
            return Fail(arg.key, "value does not fit this key");
        }
        candidate.*found->member = static_cast<float>(*parsed);
    }
    m_feature.ApplySettings(candidate);
    return GetState();
}

fx::FeatureDebugResult BloomDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
{
    if (action != "reset_defaults") {
        fx::FeatureDebugResult result;
        result.ok = false;
        result.message = "unknown action; supported: reset_defaults";
        return result;
    }
    if (!args.empty()) {
        return Fail(args.front().key, "reset_defaults takes no arguments");
    }
    m_feature.ApplySettings(BloomSettings{});
    return GetState();
}

} // namespace gte
