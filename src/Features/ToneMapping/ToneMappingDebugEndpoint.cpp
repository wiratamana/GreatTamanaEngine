#include "ToneMappingDebugEndpoint.h"

#include "ToneMappingFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace gte {

namespace {

struct SettingKey {
    std::string_view name;
    float ToneMappingSettings::* member;
};

// Every settable float key; mode is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "exposure", &ToneMappingSettings::exposure },
    SettingKey{ "saturation", &ToneMappingSettings::saturation },
    SettingKey{ "dither", &ToneMappingSettings::dither },
};

constexpr std::string_view kModeKey = "mode";

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

constexpr std::array kModes{
    std::pair{ tonemap::ToneMapMode::Clamp, std::string_view{ "Clamp" } },
    std::pair{ tonemap::ToneMapMode::Aces, std::string_view{ "Aces" } },
    std::pair{ tonemap::ToneMapMode::PbrNeutral, std::string_view{ "PbrNeutral" } },
};

std::string_view ModeName(tonemap::ToneMapMode mode) noexcept
{
    for (const auto& [value, name] : kModes) {
        if (value == mode) {
            return name;
        }
    }
    return "Aces";
}

std::optional<tonemap::ToneMapMode> ParseMode(std::string_view text) noexcept
{
    for (const auto& [value, name] : kModes) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view StatusName(ToneMappingFrameStatus status) noexcept
{
    switch (status) {
    case ToneMappingFrameStatus::NotRendered: return "NotRendered";
    case ToneMappingFrameStatus::Ok: return "Ok";
    case ToneMappingFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case ToneMappingFrameStatus::TargetFailed: return "TargetFailed";
    case ToneMappingFrameStatus::PassDisabled: return "PassDisabled";
    }
    return "NotRendered";
}

// A float shown with 6 significant digits, so 0.85f reads 0.85 and not 0.8500000238.
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

fx::FeatureDebugResult ToneMappingDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    const ToneMappingSettings& s = m_feature.Settings();
    for (const SettingKey& key : kSettingKeys) {
        Add(out, std::string(key.name), Shown(s.*key.member));
    }
    Add(out, std::string(kModeKey), std::string(ModeName(s.mode)));

    for (const ScopeName& scope : kScopes) {
        const ToneMappingViewSnapshot snapshot = m_feature.Snapshot(scope.scope);
        Add(out, std::string(scope.prefix).append("status"), std::string(StatusName(snapshot.status)));
        Add(out, std::string(scope.prefix).append("frameIndex"), Count(snapshot.frameIndex));
    }
    return out;
}

fx::FeatureDebugResult ToneMappingDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    ToneMappingSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kModeKey) {
            const auto mode = ParseMode(arg.value);
            if (!mode) {
                return Fail(arg.key, "mode must be Clamp, Aces or PbrNeutral");
            }
            candidate.mode = *mode;
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

fx::FeatureDebugResult ToneMappingDebugEndpoint::Invoke(
    std::string_view action, std::span<const fx::FeatureDebugArg> args)
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
    m_feature.ApplySettings(ToneMappingSettings{});
    return GetState();
}

} // namespace gte
