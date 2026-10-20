#include "SceneLightingDebugEndpoint.h"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace gte {

namespace {

struct SettingKey {
    std::string_view name;
    float SceneLightingSettings::* member;
};

constexpr std::array kSettingKeys{
    SettingKey{ "skyAmbientR", &SceneLightingSettings::skyAmbientR },
    SettingKey{ "skyAmbientG", &SceneLightingSettings::skyAmbientG },
    SettingKey{ "skyAmbientB", &SceneLightingSettings::skyAmbientB },
    SettingKey{ "groundAmbientR", &SceneLightingSettings::groundAmbientR },
    SettingKey{ "groundAmbientG", &SceneLightingSettings::groundAmbientG },
    SettingKey{ "groundAmbientB", &SceneLightingSettings::groundAmbientB },
    SettingKey{ "ambientIntensity", &SceneLightingSettings::ambientIntensity },
};

// Integer key; every key in the table above is a float.
constexpr std::string_view kMaterialDebugViewKey = "materialDebugView";

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

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

} // namespace

fx::FeatureDebugResult SceneLightingDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    for (const SettingKey& key : kSettingKeys) {
        out.fields.push_back({ std::string(key.name), Shown(m_settings.*key.member) });
    }
    out.fields.push_back({ std::string(kMaterialDebugViewKey),
        static_cast<std::int64_t>(static_cast<std::int32_t>(m_settings.materialDebugView)) });
    return out;
}

fx::FeatureDebugResult SceneLightingDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    SceneLightingSettings candidate = m_settings;
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kMaterialDebugViewKey) {
            const auto view = fx::ParseDebugUnsigned(arg.value);
            if (!view || *view >= static_cast<std::uint64_t>(kMaterialDebugViewCount)) {
                return Fail(arg.key, "must be 0..8");
            }
            candidate.materialDebugView = static_cast<MaterialDebugView>(*view);
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
            return Fail(arg.key, "unknown key");
        }
        const auto parsed = fx::ParseDebugFloat(arg.value);
        if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
            return Fail(arg.key, "value does not fit this key");
        }
        candidate.*found->member = static_cast<float>(*parsed);
    }
    m_settings = SanitizeSceneLightingSettings(candidate);
    return GetState();
}

fx::FeatureDebugResult SceneLightingDebugEndpoint::Invoke(
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
    m_settings = SceneLightingSettings{};
    return GetState();
}

} // namespace gte
