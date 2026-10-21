#include "SsgiDebugEndpoint.h"

#include "SsgiFeature.h"

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

using SettingMember = std::variant<bool SsgiSettings::*, std::uint32_t SsgiSettings::*, float SsgiSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView, which is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "intensity", &SsgiSettings::intensity },
    SettingKey{ "maxRayDistance", &SsgiSettings::maxRayDistance },
    SettingKey{ "thickness", &SsgiSettings::thickness },
    SettingKey{ "depthBias", &SsgiSettings::depthBias },
    SettingKey{ "rayCount", &SsgiSettings::rayCount },
    SettingKey{ "stepCount", &SsgiSettings::stepCount },
    SettingKey{ "stepDistributionPower", &SsgiSettings::stepDistributionPower },
    SettingKey{ "endFadeFraction", &SsgiSettings::endFadeFraction },
    SettingKey{ "maxRadiance", &SsgiSettings::maxRadiance },
    SettingKey{ "albedoLuma", &SsgiSettings::albedoLuma },
    SettingKey{ "albedoSaturation", &SsgiSettings::albedoSaturation },
    SettingKey{ "maxDistance", &SsgiSettings::maxDistance },
    SettingKey{ "distanceFadeFraction", &SsgiSettings::distanceFadeFraction },
    SettingKey{ "resolutionDivisor", &SsgiSettings::resolutionDivisor },
    SettingKey{ "spatialDenoise", &SsgiSettings::spatialDenoise },
    SettingKey{ "temporal", &SsgiSettings::temporal },
    SettingKey{ "historyWeight", &SsgiSettings::historyWeight },
    SettingKey{ "depthSigma", &SsgiSettings::depthSigma },
    SettingKey{ "applyInSceneView", &SsgiSettings::applyInSceneView },
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

constexpr std::array kDebugViews{ SsgiDebugView::Off, SsgiDebugView::GiOnly, SsgiDebugView::Normals };

std::string_view DebugViewName(SsgiDebugView view) noexcept
{
    switch (view) {
    case SsgiDebugView::Off: return "Off";
    case SsgiDebugView::GiOnly: return "GiOnly";
    case SsgiDebugView::Normals: return "Normals";
    }
    return "Off";
}

std::optional<SsgiDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (const SsgiDebugView view : kDebugViews) {
        if (text == DebugViewName(view)) {
            return view;
        }
    }
    return std::nullopt;
}

std::string_view StatusName(SsgiFrameStatus status) noexcept
{
    switch (status) {
    case SsgiFrameStatus::NotRendered: return "NotRendered";
    case SsgiFrameStatus::Ok: return "Ok";
    case SsgiFrameStatus::NoValidCamera: return "NoValidCamera";
    case SsgiFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case SsgiFrameStatus::TargetFailed: return "TargetFailed";
    case SsgiFrameStatus::PassDisabled: return "PassDisabled";
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

void AddSettings(fx::FeatureDebugResult& out, const SsgiSettings& s)
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

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const SsgiViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };

    Add(out, key("status"), std::string(StatusName(v.status)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("giExtent.w"), static_cast<std::int64_t>(v.giExtent.width));
    Add(out, key("giExtent.h"), static_cast<std::int64_t>(v.giExtent.height));
    Add(out, key("divisor"), static_cast<std::int64_t>(v.divisor));
    Add(out, key("historyUsed"), v.historyUsed);
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

// Parses `text` into the member the key names. False when the text does not fit the member type.
bool AssignSetting(SsgiSettings& s, const SettingKey& key, std::string_view text)
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

fx::FeatureDebugResult SsgiDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult SsgiDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    SsgiSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be Off, GiOnly or Normals");
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

fx::FeatureDebugResult SsgiDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
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
    m_feature.ApplySettings(SsgiSettings{});
    return GetState();
}

} // namespace gte
