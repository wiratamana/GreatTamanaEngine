#include "AtmosphereDebugEndpoint.h"

#include "AtmosphereFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace gte {

namespace {

using SettingMember = std::variant<bool AtmosphereSettings::*, int AtmosphereSettings::*, float AtmosphereSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every writable key. Everything else is read-only state.
constexpr std::array kSettingKeys{
    SettingKey{ "sceneLightFollowsAtmosphere", &AtmosphereSettings::sceneLightFollowsAtmosphere },
    SettingKey{ "autoScale", &AtmosphereSettings::autoScale },
    SettingKey{ "aerialPerspectiveStrength", &AtmosphereSettings::aerialPerspectiveStrength },
    SettingKey{ "aerialPerspectiveMaxDistanceKm", &AtmosphereSettings::aerialPerspectiveMaxDistanceKm },
    SettingKey{ "aerialPerspectiveDepthExponent", &AtmosphereSettings::aerialPerspectiveDepthExponent },
    SettingKey{ "aerialPerspectiveSamplesPerSlice", &AtmosphereSettings::aerialPerspectiveSamplesPerSlice },
    SettingKey{ "aerialPerspectiveScatteringExaggeration", &AtmosphereSettings::aerialPerspectiveScatteringExaggeration },
};

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

std::string_view ReceiverName(AerialReceiverState state) noexcept
{
    switch (state) {
    case AerialReceiverState::NotChecked: return "NotChecked";
    case AerialReceiverState::Used: return "Used";
    case AerialReceiverState::Missing: return "Missing";
    case AerialReceiverState::Rejected: return "Rejected";
    case AerialReceiverState::Mismatch: return "Mismatch";
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

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

void AddSettings(fx::FeatureDebugResult& out, const AtmosphereSettings& s)
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
}

// Parses `text` into the member the key names. False when the text does not fit the member type.
bool AssignSetting(AtmosphereSettings& s, const SettingKey& key, std::string_view text)
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
                if (!parsed || !std::isfinite(*parsed) || std::abs(*parsed) > std::numeric_limits<float>::max()) {
                    return false;
                }
                s.*member = static_cast<float>(*parsed);
                return true;
            } else {
                const auto parsed = fx::ParseDebugUnsigned(text);
                if (!parsed || *parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                    return false;
                }
                s.*member = static_cast<Value>(*parsed);
                return true;
            }
        },
        key.member);
}

} // namespace

fx::FeatureDebugResult AtmosphereDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    for (const ScopeName& scope : kScopes) {
        const auto key = [&](std::string_view name) { return std::string(scope.prefix).append(name); };

        const SunLightModulationSnapshot snapshot = m_feature.SunLightSnapshot(scope.scope);
        Add(out, key("active"), snapshot.light.active);
        Add(out, key("sunTintR"), static_cast<double>(snapshot.light.sunTint.x));
        Add(out, key("sunTintG"), static_cast<double>(snapshot.light.sunTint.y));
        Add(out, key("sunTintB"), static_cast<double>(snapshot.light.sunTint.z));
        Add(out, key("ambientScale"), static_cast<double>(snapshot.light.ambientScale));
        Add(out, key("frameIndex"), Count(snapshot.frameIndex));

        const AtmosphereAerialSnapshot aerial = m_feature.AerialSnapshot(scope.scope);
        Add(out, key("aerial.maxDistanceKm"), Shown(aerial.maxDistanceKm));
        Add(out, key("aerial.exaggeration"), Shown(aerial.exaggeration));
        Add(out, key("aerial.shadowBlend"), Shown(aerial.shadowBlend));
        Add(out, key("aerial.cascadeCount"), static_cast<std::int64_t>(aerial.cascadeCount));
        Add(out, key("aerial.receiver"), std::string(ReceiverName(aerial.receiver)));
        Add(out, key("aerial.scaleHeld"), aerial.scaleHeld);
        Add(out, key("aerial.frameIndex"), Count(aerial.frameIndex));
    }
    return out;
}

fx::FeatureDebugResult AtmosphereDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    AtmosphereSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        const auto found = std::find_if(
            kSettingKeys.begin(), kSettingKeys.end(), [&](const SettingKey& key) { return key.name == arg.key; });
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

fx::FeatureDebugResult AtmosphereDebugEndpoint::Invoke(std::string_view, std::span<const fx::FeatureDebugArg>)
{
    return Fail({}, "no actions");
}

} // namespace gte
