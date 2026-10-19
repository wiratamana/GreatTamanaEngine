#include "ShadowDebugEndpoint.h"

#include "ShadowFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

namespace gte {

namespace {

using SettingMember = std::variant<bool ShadowSettings::*, std::uint32_t ShadowSettings::*, float ShadowSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView, which is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "cascadeCount", &ShadowSettings::cascadeCount },
    SettingKey{ "mapResolution", &ShadowSettings::mapResolution },
    SettingKey{ "maxDistance", &ShadowSettings::maxDistance },
    SettingKey{ "splitLambda", &ShadowSettings::splitLambda },
    SettingKey{ "cascadeBlendFraction", &ShadowSettings::cascadeBlendFraction },
    SettingKey{ "distanceFadeFraction", &ShadowSettings::distanceFadeFraction },
    SettingKey{ "casterRange", &ShadowSettings::casterRange },
    SettingKey{ "cullCasters", &ShadowSettings::cullCasters },
    SettingKey{ "casterCullPaddingFraction", &ShadowSettings::casterCullPaddingFraction },
    SettingKey{ "constBiasTexels", &ShadowSettings::constBiasTexels },
    SettingKey{ "slopeBiasTexels", &ShadowSettings::slopeBiasTexels },
    SettingKey{ "normalOffsetTexels", &ShadowSettings::normalOffsetTexels },
    SettingKey{ "filterRadiusTexels", &ShadowSettings::filterRadiusTexels },
    SettingKey{ "filterSamples", &ShadowSettings::filterSamples },
    SettingKey{ "strength", &ShadowSettings::strength },
    SettingKey{ "stabilizeCascades", &ShadowSettings::stabilizeCascades },
    SettingKey{ "useSdsm", &ShadowSettings::useSdsm },
    SettingKey{ "sdsmPaddingFraction", &ShadowSettings::sdsmPaddingFraction },
    SettingKey{ "sdsmShrinkSeconds", &ShadowSettings::sdsmShrinkSeconds },
    SettingKey{ "sdsmQuantizeSteps", &ShadowSettings::sdsmQuantizeSteps },
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

std::string_view DebugViewName(ShadowDebugView view) noexcept
{
    switch (view) {
    case ShadowDebugView::Off: return "Off";
    case ShadowDebugView::CascadeColors: return "CascadeColors";
    case ShadowDebugView::Mask: return "Mask";
    }
    return "Off";
}

std::optional<ShadowDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (const ShadowDebugView view : { ShadowDebugView::Off, ShadowDebugView::CascadeColors, ShadowDebugView::Mask }) {
        if (text == DebugViewName(view)) {
            return view;
        }
    }
    return std::nullopt;
}

std::string_view StatusName(ShadowFrameStatus status) noexcept
{
    switch (status) {
    case ShadowFrameStatus::NotRendered: return "NotRendered";
    case ShadowFrameStatus::Ok: return "Ok";
    case ShadowFrameStatus::NoValidCamera: return "NoValidCamera";
    case ShadowFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case ShadowFrameStatus::TargetFailed: return "TargetFailed";
    case ShadowFrameStatus::PassDisabled: return "PassDisabled";
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

void AddSettings(fx::FeatureDebugResult& out, const ShadowSettings& s)
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

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const ShadowViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };

    Add(out, key("status"), std::string(StatusName(v.status)));
    Add(out, key("mask.status"), std::string(StatusName(v.maskStatus)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("sunElevationDegrees"), Shown(v.sunElevationDegrees));
    Add(out, key("effectiveMaxDistance"), Shown(v.effectiveMaxDistance));
    Add(out, key("resolutionInUse"), static_cast<std::int64_t>(v.resolutionInUse));
    Add(out, key("cascadeBytes"), Count(v.cascadeBytes));
    Add(out, key("timing.deltaSeconds"), Shown(v.deltaSeconds));

    for (std::uint32_t i = 0; i < v.cascades.count && i < kMaxShadowCascades; ++i) {
        const ShadowCascade& c = v.cascades.cascades[i];
        const std::string base = "cascade" + std::to_string(i) + ".";
        Add(out, key(base + "sliceStart"), Shown(c.sliceStart));
        Add(out, key(base + "sliceEnd"), Shown(c.sliceEnd));
        Add(out, key(base + "radius"), Shown(c.radius));
        Add(out, key(base + "texelWorldSize"), Shown(c.texelWorldSize));
        Add(out, key(base + "depthRange"), Shown(c.depthRange));
        Add(out, key(base + "gridX"), c.gridX);
        Add(out, key(base + "gridY"), c.gridY);
    }

    Add(out, key("sdsm.active"), v.cascades.sdsmActive);
    Add(out, key("sdsm.rawNear"), Shown(v.sdsm.raw.nearDepth));
    Add(out, key("sdsm.rawFar"), Shown(v.sdsm.raw.farDepth));
    Add(out, key("sdsm.nearDepth"), Shown(v.sdsm.used.nearDepth));
    Add(out, key("sdsm.farDepth"), Shown(v.sdsm.used.farDepth));
    Add(out, key("sdsm.ageFrames"), Count(v.sdsmAgeFrames));
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
bool AssignSetting(ShadowSettings& s, const SettingKey& key, std::string_view text)
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

fx::FeatureDebugResult ShadowDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult ShadowDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    ShadowSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be Off, CascadeColors or Mask");
            }
            candidate.debugView = *view;
            continue;
        }
        const SettingKey* found = nullptr;
        for (const SettingKey& key : kSettingKeys) {
            if (key.name == arg.key) {
                found = &key;
                break;
            }
        }
        if (found == nullptr) {
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

fx::FeatureDebugResult ShadowDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
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
    m_feature.ApplySettings(ShadowSettings{});
    return GetState();
}

} // namespace gte
