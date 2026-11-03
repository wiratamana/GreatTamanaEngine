#include "ToonCharacterDebugEndpoint.h"

#include "ToonCharacterFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <utility>

namespace gte {

namespace {

struct FloatKey {
    std::string_view name;
    float ToonCharacterSettings::* member;
};

constexpr std::array kFloatKeys{
    FloatKey{ "bandThreshold", &ToonCharacterSettings::bandThreshold },
    FloatKey{ "bandSoftness", &ToonCharacterSettings::bandSoftness },
    FloatKey{ "deepThreshold", &ToonCharacterSettings::deepThreshold },
    FloatKey{ "shadowStrength", &ToonCharacterSettings::shadowStrength },
    FloatKey{ "shadeBlend", &ToonCharacterSettings::shadeBlend },
    FloatKey{ "rimStrength", &ToonCharacterSettings::rimStrength },
    FloatKey{ "rimWidth", &ToonCharacterSettings::rimWidth },
    FloatKey{ "hairHighlight", &ToonCharacterSettings::hairHighlight },
    FloatKey{ "emissiveBoost", &ToonCharacterSettings::emissiveBoost },
    FloatKey{ "faceFlatten", &ToonCharacterSettings::faceFlatten },
    FloatKey{ "outlineWidthPx", &ToonCharacterSettings::outlineWidthPx },
    FloatKey{ "outlineDarkness", &ToonCharacterSettings::outlineDarkness },
};

struct BoolKey {
    std::string_view name;
    bool ToonCharacterSettings::* member;
};

constexpr std::array kBoolKeys{
    BoolKey{ "enabled", &ToonCharacterSettings::enabled },
    BoolKey{ "gemHeuristic", &ToonCharacterSettings::gemHeuristic },
    BoolKey{ "goldGlint", &ToonCharacterSettings::goldGlint },
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

std::string_view StatusName(ToonCharacterFrameStatus status) noexcept
{
    switch (status) {
    case ToonCharacterFrameStatus::NotRendered: return "NotRendered";
    case ToonCharacterFrameStatus::Ok: return "Ok";
    case ToonCharacterFrameStatus::FeatureDisabled: return "FeatureDisabled";
    case ToonCharacterFrameStatus::NoToonParts: return "NoToonParts";
    case ToonCharacterFrameStatus::PassDisabled: return "PassDisabled";
    case ToonCharacterFrameStatus::CaptureArmed: return "CaptureArmed";
    case ToonCharacterFrameStatus::DrawRefused: return "DrawRefused";
    }
    return "NotRendered";
}

// A float shown with 6 significant digits, so 0.6f reads 0.6 and not 0.6000000238.
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

std::optional<ToonDebugArgError> ParseToonCharacterDebugArgs(
    ToonCharacterSettings& candidate, std::span<const fx::FeatureDebugArg> args)
{
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = fx::ParseDebugUnsigned(arg.value);
            if (!view) {
                return ToonDebugArgError{ arg.key, "must be a whole number 0..5" };
            }
            candidate.debugView = static_cast<std::int32_t>(
                std::min<std::uint64_t>(*view, static_cast<std::uint64_t>(kToonDebugViewCount - 1)));
            continue;
        }
        const auto boolKey = std::find_if(
            kBoolKeys.begin(), kBoolKeys.end(), [&](const BoolKey& key) { return key.name == arg.key; });
        if (boolKey != kBoolKeys.end()) {
            const auto parsed = fx::ParseDebugBool(arg.value);
            if (!parsed) {
                return ToonDebugArgError{ arg.key, "must be true, false, 1 or 0" };
            }
            candidate.*boolKey->member = *parsed;
            continue;
        }
        const auto floatKey = std::find_if(
            kFloatKeys.begin(), kFloatKeys.end(), [&](const FloatKey& key) { return key.name == arg.key; });
        if (floatKey == kFloatKeys.end()) {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.");
            return ToonDebugArgError{ arg.key, readOnly ? "key is read-only" : "unknown key" };
        }
        const auto parsed = fx::ParseDebugFloat(arg.value);
        if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
            return ToonDebugArgError{ arg.key, "value does not fit this key" };
        }
        candidate.*floatKey->member = static_cast<float>(*parsed);
    }
    return std::nullopt;
}

fx::FeatureDebugResult ToonCharacterDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    const ToonCharacterSettings& s = m_feature.Settings();
    for (const BoolKey& key : kBoolKeys) {
        Add(out, std::string(key.name), s.*key.member);
    }
    for (const FloatKey& key : kFloatKeys) {
        Add(out, std::string(key.name), Shown(s.*key.member));
    }
    Add(out, std::string(kDebugViewKey), static_cast<std::int64_t>(s.debugView));

    for (const ScopeName& scope : kScopes) {
        const ToonCharacterViewSnapshot snapshot = m_feature.Snapshot(scope.scope);
        const auto key = [&](std::string_view name) { return std::string(scope.prefix).append(name); };
        Add(out, key("status"), std::string(StatusName(snapshot.status)));
        Add(out, key("frameIndex"), Count(snapshot.frameIndex));
        Add(out, key("candidatePartCount"), static_cast<std::int64_t>(snapshot.candidatePartCount));
        Add(out, key("claimedEntityCount"), static_cast<std::int64_t>(snapshot.claimedEntityCount));
    }
    return out;
}

fx::FeatureDebugResult ToonCharacterDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    ToonCharacterSettings candidate = m_feature.Settings();
    if (const auto error = ParseToonCharacterDebugArgs(candidate, args)) {
        return Fail(error->key, error->message);
    }
    m_feature.ApplySettings(candidate);
    return GetState();
}

fx::FeatureDebugResult ToonCharacterDebugEndpoint::Invoke(
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
    m_feature.ApplySettings(ToonCharacterSettings{});
    return GetState();
}

} // namespace gte
