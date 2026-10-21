#include "AtmosphereDebugEndpoint.h"

#include "AtmosphereFeature.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace gte {

namespace {

constexpr std::string_view kFollowKey = "sceneLightFollowsAtmosphere";

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::GameView, "game." },
    ScopeName{ rg::ViewScope::SceneView, "scene." },
};

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

fx::FeatureDebugResult AtmosphereDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    Add(out, std::string(kFollowKey), m_feature.Settings().sceneLightFollowsAtmosphere);
    for (const ScopeName& scope : kScopes) {
        const SunLightModulationSnapshot snapshot = m_feature.SunLightSnapshot(scope.scope);
        const auto key = [&](std::string_view name) { return std::string(scope.prefix).append(name); };
        Add(out, key("active"), snapshot.light.active);
        Add(out, key("sunTintR"), static_cast<double>(snapshot.light.sunTint.x));
        Add(out, key("sunTintG"), static_cast<double>(snapshot.light.sunTint.y));
        Add(out, key("sunTintB"), static_cast<double>(snapshot.light.sunTint.z));
        Add(out, key("ambientScale"), static_cast<double>(snapshot.light.ambientScale));
        Add(out, key("frameIndex"),
            static_cast<std::int64_t>(
                std::min<std::uint64_t>(snapshot.frameIndex, std::numeric_limits<std::int64_t>::max())));
    }
    return out;
}

fx::FeatureDebugResult AtmosphereDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    bool follow = m_feature.Settings().sceneLightFollowsAtmosphere;
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key != kFollowKey) {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.");
            return Fail(arg.key, readOnly ? "key is read-only" : "unknown key");
        }
        const auto parsed = fx::ParseDebugBool(arg.value);
        if (!parsed) {
            return Fail(arg.key, "value does not fit this key");
        }
        follow = *parsed;
    }
    m_feature.Settings().sceneLightFollowsAtmosphere = follow;
    return GetState();
}

fx::FeatureDebugResult AtmosphereDebugEndpoint::Invoke(std::string_view, std::span<const fx::FeatureDebugArg>)
{
    return Fail({}, "no actions");
}

} // namespace gte
