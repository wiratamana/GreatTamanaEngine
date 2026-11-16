#include "WaterDebugEndpoint.h"

#include "WaterFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace gte {

namespace {

using SettingMember = std::variant<bool WaterSettings::*, std::uint32_t WaterSettings::*, float WaterSettings::*,
    Vec3 WaterSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView, which is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "enabled", &WaterSettings::enabled },
    SettingKey{ "seaLevel", &WaterSettings::seaLevel },
    SettingKey{ "waveCount", &WaterSettings::waveCount },
    SettingKey{ "waveHeight", &WaterSettings::waveHeight },
    SettingKey{ "choppiness", &WaterSettings::choppiness },
    SettingKey{ "waveSpeed", &WaterSettings::waveSpeed },
    SettingKey{ "windDirectionDeg", &WaterSettings::windDirectionDeg },
    SettingKey{ "windSpread", &WaterSettings::windSpread },
    SettingKey{ "baseWavelength", &WaterSettings::baseWavelength },
    SettingKey{ "rippleStrength", &WaterSettings::rippleStrength },
    SettingKey{ "rippleScale", &WaterSettings::rippleScale },
    SettingKey{ "waveLodScale", &WaterSettings::waveLodScale },
    SettingKey{ "shallowColor", &WaterSettings::shallowColor },
    SettingKey{ "deepColor", &WaterSettings::deepColor },
    SettingKey{ "foamColor", &WaterSettings::foamColor },
    SettingKey{ "clarity", &WaterSettings::clarity },
    SettingKey{ "absorption", &WaterSettings::absorption },
    SettingKey{ "refractionStrength", &WaterSettings::refractionStrength },
    SettingKey{ "roughness", &WaterSettings::roughness },
    SettingKey{ "sunSpecular", &WaterSettings::sunSpecular },
    SettingKey{ "reflectionStrength", &WaterSettings::reflectionStrength },
    SettingKey{ "shadowStrength", &WaterSettings::shadowStrength },
    SettingKey{ "crestFoam", &WaterSettings::crestFoam },
    SettingKey{ "shoreFoam", &WaterSettings::shoreFoam },
    SettingKey{ "shoreWidth", &WaterSettings::shoreWidth },
    SettingKey{ "ssr", &WaterSettings::ssr },
    SettingKey{ "ssrSteps", &WaterSettings::ssrSteps },
    SettingKey{ "ssrMaxDistance", &WaterSettings::ssrMaxDistance },
    SettingKey{ "wetShore", &WaterSettings::wetShore },
    SettingKey{ "wetBand", &WaterSettings::wetBand },
    SettingKey{ "wetStrength", &WaterSettings::wetStrength },
    SettingKey{ "wetFadeDistance", &WaterSettings::wetFadeDistance },
    SettingKey{ "maxDistance", &WaterSettings::maxDistance },
};

constexpr std::string_view kDebugViewKey = "debugView";

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

constexpr std::array<std::string_view, kWaterDebugViewCount> kDebugViewNames{ "Off", "Depth", "WaterDepth", "ShoreMask",
    "Foam", "Normal", "WaveHeight", "Reflection", "BodyId", "WetMask" };

std::string_view DebugViewName(WaterDebugView view) noexcept
{
    const auto index = static_cast<std::size_t>(view);
    return index < kDebugViewNames.size() ? kDebugViewNames[index] : kDebugViewNames[0];
}

std::optional<WaterDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (std::size_t i = 0; i < kDebugViewNames.size(); ++i) {
        if (text == kDebugViewNames[i]) {
            return static_cast<WaterDebugView>(i);
        }
    }
    return std::nullopt;
}

std::string_view StatusName(WaterFrameStatus status) noexcept
{
    switch (status) {
    case WaterFrameStatus::NotRendered: return "NotRendered";
    case WaterFrameStatus::Ok: return "Ok";
    case WaterFrameStatus::NoValidCamera: return "NoValidCamera";
    case WaterFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case WaterFrameStatus::TargetFailed: return "TargetFailed";
    case WaterFrameStatus::PassDisabled: return "PassDisabled";
    case WaterFrameStatus::Disabled: return "Disabled";
    case WaterFrameStatus::CameraUnderwater: return "CameraUnderwater";
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

std::string Vec3Text(const Vec3& v)
{
    return std::to_string(Shown(v.x)) + "," + std::to_string(Shown(v.y)) + "," + std::to_string(Shown(v.z));
}

std::int64_t Count(std::uint64_t value) noexcept
{
    return static_cast<std::int64_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::int64_t>::max()));
}

void Add(fx::FeatureDebugResult& out, std::string key, fx::FeatureDebugValue value)
{
    out.fields.push_back({ std::move(key), std::move(value) });
}

void AddSettings(fx::FeatureDebugResult& out, const WaterSettings& s)
{
    for (const SettingKey& key : kSettingKeys) {
        std::visit(
            [&](auto member) {
                using Value = std::remove_cvref_t<decltype(s.*member)>;
                if constexpr (std::is_same_v<Value, bool>) {
                    Add(out, std::string(key.name), s.*member);
                } else if constexpr (std::is_same_v<Value, float>) {
                    Add(out, std::string(key.name), Shown(s.*member));
                } else if constexpr (std::is_same_v<Value, Vec3>) {
                    Add(out, std::string(key.name), Vec3Text(s.*member));
                } else {
                    Add(out, std::string(key.name), static_cast<std::int64_t>(s.*member));
                }
            },
            key.member);
    }
    Add(out, std::string(kDebugViewKey), std::string(DebugViewName(s.debugView)));
}

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const WaterViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };

    Add(out, key("status"), std::string(StatusName(v.status)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("extent.w"), static_cast<std::int64_t>(v.extent.width));
    Add(out, key("extent.h"), static_cast<std::int64_t>(v.extent.height));
    Add(out, key("waveCount"), static_cast<std::int64_t>(v.waveCount));
    Add(out, key("bodyCount"), static_cast<std::int64_t>(v.bodyCount));
    Add(out, key("skyValid"), v.skyValid);
    Add(out, key("textureBytes"), Count(v.textureBytes));
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

std::optional<Vec3> ParseVec3(std::string_view text) noexcept
{
    std::array<float, 3> parts{};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t comma = text.find(',');
        if ((i < 2) == (comma == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto parsed = fx::ParseDebugFloat(text.substr(0, comma));
        if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
            return std::nullopt;
        }
        parts[i] = static_cast<float>(*parsed);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return Vec3(parts[0], parts[1], parts[2]);
}

// Parses `text` into the member the key names. False when the text does not fit the member type.
bool AssignSetting(WaterSettings& s, const SettingKey& key, std::string_view text)
{
    return std::visit(
        [&](auto member) {
            using Value = std::remove_cvref_t<decltype(s.*member)>;
            if constexpr (std::is_same_v<Value, bool>) {
                const auto parsed = fx::ParseDebugBool(text);
                if (parsed) {
                    s.*member = *parsed;
                }
                return parsed.has_value();
            } else if constexpr (std::is_same_v<Value, float>) {
                const auto parsed = fx::ParseDebugFloat(text);
                if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
                    return false;
                }
                s.*member = static_cast<float>(*parsed);
                return true;
            } else if constexpr (std::is_same_v<Value, Vec3>) {
                const auto parsed = ParseVec3(text);
                if (parsed) {
                    s.*member = *parsed;
                }
                return parsed.has_value();
            } else {
                const auto parsed = fx::ParseDebugUnsigned(text);
                if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                s.*member = static_cast<Value>(*parsed);
                return true;
            }
        },
        key.member);
}

} // namespace

fx::FeatureDebugResult WaterDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult WaterDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    WaterSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be one of Off, Depth, WaterDepth, ShoreMask, Foam, Normal, "
                                     "WaveHeight, Reflection, BodyId, WetMask");
            }
            candidate.debugView = *view;
            continue;
        }
        const auto found = std::find_if(kSettingKeys.begin(), kSettingKeys.end(),
            [&](const SettingKey& key) { return key.name == arg.key; });
        if (found == kSettingKeys.end()) {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.");
            return Fail(arg.key, readOnly ? "key is read-only" : "unknown key");
        }
        if (!AssignSetting(candidate, *found, arg.value)) {
            return Fail(arg.key, "value does not fit this key");
        }
    }
    m_feature.ApplySettings(candidate);
    return GetState();
}

fx::FeatureDebugResult WaterDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
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
    m_feature.ApplySettings(WaterSettings{});
    return GetState();
}

} // namespace gte
