#include "TemporalAaDebugEndpoint.h"

#include "TemporalAaFeature.h"
#include "TemporalAaMath.h"

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

using SettingMember = std::variant<bool TemporalAaSettings::*, std::uint32_t TemporalAaSettings::*, float TemporalAaSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView, which is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "enabled", &TemporalAaSettings::enabled },
    SettingKey{ "jitterSampleCount", &TemporalAaSettings::jitterSampleCount },
    SettingKey{ "jitterScale", &TemporalAaSettings::jitterScale },
    SettingKey{ "feedbackMin", &TemporalAaSettings::feedbackMin },
    SettingKey{ "feedbackMax", &TemporalAaSettings::feedbackMax },
    SettingKey{ "clipGamma", &TemporalAaSettings::clipGamma },
    SettingKey{ "depthTolerance", &TemporalAaSettings::depthTolerance },
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

std::optional<TemporalAaDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (const TemporalAaDebugView view : { TemporalAaDebugView::Off, TemporalAaDebugView::JitterOnly }) {
        if (text == TemporalAaDebugViewName(view)) {
            return view;
        }
    }
    return std::nullopt;
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

void AddSettings(fx::FeatureDebugResult& out, const TemporalAaSettings& s)
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
    Add(out, std::string(kDebugViewKey), std::string(TemporalAaDebugViewName(s.debugView)));
}

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const TemporalAaViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };

    Add(out, key("status"), std::string(TemporalAaStatusName(v.status)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("jitterDecision"), std::string(TemporalAaJitterDecisionName(v.jitterDecision)));
    Add(out, key("jitterFrameIndex"), Count(v.jitterFrameIndex));
    Add(out, key("jitterApplied"), v.jitterApplied);
    Add(out, key("jitterPixelX"), Shown(v.jitterPixelX));
    Add(out, key("jitterPixelY"), Shown(v.jitterPixelY));
    Add(out, key("historyUsed"), v.historyUsed);
    Add(out, key("stableFrames"), static_cast<std::int64_t>(v.stableFrames));
    Add(out, key("extentWidth"), static_cast<std::int64_t>(v.extent.width));
    Add(out, key("extentHeight"), static_cast<std::int64_t>(v.extent.height));
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
bool AssignSetting(TemporalAaSettings& s, const SettingKey& key, std::string_view text)
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

fx::FeatureDebugResult TemporalAaDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult TemporalAaDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    TemporalAaSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be Off or JitterOnly");
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

fx::FeatureDebugResult TemporalAaDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
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
    m_feature.ApplySettings(TemporalAaSettings{});
    return GetState();
}

} // namespace gte
