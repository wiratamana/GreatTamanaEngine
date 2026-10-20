#include "VolumetricLightDebugEndpoint.h"

#include "VolumetricLightFeature.h"

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

using SettingMember = std::variant<bool VolumetricLightSettings::*, std::uint32_t VolumetricLightSettings::*,
    float VolumetricLightSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView, which is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "enabled", &VolumetricLightSettings::enabled },
    SettingKey{ "applyInSceneView", &VolumetricLightSettings::applyInSceneView },
    SettingKey{ "density", &VolumetricLightSettings::density },
    SettingKey{ "albedo", &VolumetricLightSettings::albedo },
    SettingKey{ "anisotropyG", &VolumetricLightSettings::anisotropyG },
    SettingKey{ "heightDensity", &VolumetricLightSettings::heightDensity },
    SettingKey{ "heightBase", &VolumetricLightSettings::heightBase },
    SettingKey{ "heightFalloff", &VolumetricLightSettings::heightFalloff },
    SettingKey{ "noiseAmount", &VolumetricLightSettings::noiseAmount },
    SettingKey{ "noiseScale", &VolumetricLightSettings::noiseScale },
    SettingKey{ "windX", &VolumetricLightSettings::windX },
    SettingKey{ "windZ", &VolumetricLightSettings::windZ },
    SettingKey{ "maxDistance", &VolumetricLightSettings::maxDistance },
    SettingKey{ "startDistance", &VolumetricLightSettings::startDistance },
    SettingKey{ "depthExponent", &VolumetricLightSettings::depthExponent },
    SettingKey{ "sunIntensity", &VolumetricLightSettings::sunIntensity },
    SettingKey{ "ambientR", &VolumetricLightSettings::ambientR },
    SettingKey{ "ambientG", &VolumetricLightSettings::ambientG },
    SettingKey{ "ambientB", &VolumetricLightSettings::ambientB },
    SettingKey{ "ambientIntensity", &VolumetricLightSettings::ambientIntensity },
    SettingKey{ "ambientShadowedFraction", &VolumetricLightSettings::ambientShadowedFraction },
    SettingKey{ "shadowSamples", &VolumetricLightSettings::shadowSamples },
    SettingKey{ "shadowFilterTexels", &VolumetricLightSettings::shadowFilterTexels },
    SettingKey{ "shadowBiasTexels", &VolumetricLightSettings::shadowBiasTexels },
    SettingKey{ "temporal", &VolumetricLightSettings::temporal },
    SettingKey{ "jitter", &VolumetricLightSettings::jitter },
    SettingKey{ "historyBlend", &VolumetricLightSettings::historyBlend },
    SettingKey{ "strength", &VolumetricLightSettings::strength },
    SettingKey{ "skyDistanceFraction", &VolumetricLightSettings::skyDistanceFraction },
    SettingKey{ "blendInLinearSpace", &VolumetricLightSettings::blendInLinearSpace },
    SettingKey{ "dither", &VolumetricLightSettings::dither },
};

constexpr std::string_view kDebugViewKey = "debugView";

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::GameView, "game." },
    ScopeName{ rg::ViewScope::SceneView, "scene." },
};

constexpr std::array kDebugViews{
    std::pair{ VolumetricLightDebugView::Off, std::string_view{ "Off" } },
    std::pair{ VolumetricLightDebugView::InScatterOnly, std::string_view{ "InScatterOnly" } },
    std::pair{ VolumetricLightDebugView::Transmittance, std::string_view{ "Transmittance" } },
};

std::string_view DebugViewName(VolumetricLightDebugView view) noexcept
{
    for (const auto& [value, name] : kDebugViews) {
        if (value == view) {
            return name;
        }
    }
    return "Off";
}

std::optional<VolumetricLightDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (const auto& [value, name] : kDebugViews) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view StatusName(VolumetricLightFrameStatus status) noexcept
{
    switch (status) {
    case VolumetricLightFrameStatus::NotRendered: return "NotRendered";
    case VolumetricLightFrameStatus::Ok: return "Ok";
    case VolumetricLightFrameStatus::Disabled: return "Disabled";
    case VolumetricLightFrameStatus::NoValidCamera: return "NoValidCamera";
    case VolumetricLightFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case VolumetricLightFrameStatus::TargetFailed: return "TargetFailed";
    case VolumetricLightFrameStatus::PassDisabled: return "PassDisabled";
    }
    return "NotRendered";
}

std::string_view ReceiverName(VolumetricLightReceiverState state) noexcept
{
    switch (state) {
    case VolumetricLightReceiverState::NotChecked: return "NotChecked";
    case VolumetricLightReceiverState::Used: return "Used";
    case VolumetricLightReceiverState::Missing: return "Missing";
    case VolumetricLightReceiverState::Rejected: return "Rejected";
    }
    return "NotChecked";
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

void AddSettings(fx::FeatureDebugResult& out, const VolumetricLightSettings& s)
{
    for (const SettingKey& key : kSettingKeys) {
        std::visit(
            [&](auto member) {
                using Value = std::remove_cvref_t<decltype(s.*member)>;
                if constexpr (std::is_same_v<Value, bool>) {
                    Add(out, std::string(key.name), s.*member);
                } else if constexpr (std::is_same_v<Value, float>) {
                    Add(out, std::string(key.name), Shown(s.*member));
                } else {
                    Add(out, std::string(key.name), static_cast<std::int64_t>(s.*member));
                }
            },
            key.member);
    }
    Add(out, std::string(kDebugViewKey), std::string(DebugViewName(s.debugView)));
}

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const VolumetricLightViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };

    Add(out, key("status"), std::string(StatusName(v.status)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("receiver"), std::string(ReceiverName(v.receiver)));
    Add(out, key("shadowAvailable"), v.receiver == VolumetricLightReceiverState::Used);
    Add(out, key("cascadeCount"), static_cast<std::int64_t>(v.cascadeCount));
    Add(out, key("effectiveMaxDistance"), Shown(v.effectiveMaxDistance));
    Add(out, key("historyUsed"), v.historyUsed);
    Add(out, key("jitterIndex"), static_cast<std::int64_t>(v.jitterIndex));
    Add(out, key("textureBytes"), Count(v.textureBytes));
    Add(out, key("grid"),
        std::to_string(kVolumetricLightGridWidth) + "x" + std::to_string(kVolumetricLightGridHeight) + "x"
            + std::to_string(kVolumetricLightGridDepth));
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

// Parses `text` into the member the key names. False when the text does not fit the member type.
bool AssignSetting(VolumetricLightSettings& s, const SettingKey& key, std::string_view text)
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

fx::FeatureDebugResult VolumetricLightDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult VolumetricLightDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    VolumetricLightSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be Off, InScatterOnly or Transmittance");
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

fx::FeatureDebugResult VolumetricLightDebugEndpoint::Invoke(
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
    m_feature.ApplySettings(VolumetricLightSettings{});
    return GetState();
}

} // namespace gte
