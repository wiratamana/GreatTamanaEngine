#include "GpuDrivenSceneDebugEndpoint.h"

#include "GpuDrivenSceneFeature.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

namespace gte {

namespace {

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::GameView, "game." },
    ScopeName{ rg::ViewScope::SceneView, "scene." },
};

std::int64_t Count(std::uint64_t value) noexcept
{
    return static_cast<std::int64_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::int64_t>::max()));
}

void Add(fx::FeatureDebugResult& out, std::string key, fx::FeatureDebugValue value)
{
    out.fields.push_back({ std::move(key), std::move(value) });
}

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const GpuDrivenSceneViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };
    Add(out, key("rendered"), v.rendered);
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("frozen"), v.frozen);
    Add(out, key("occlusionUsable"), v.occlusionUsable);
    Add(out, key("inertReason"), std::string(ClusterInertReasonName(v.inertReason)));
    Add(out, key("lastRefusalReason"), v.refusal);
    Add(out, key("claimedEntities"), Count(v.claimedEntities));
    Add(out, key("parts"), Count(v.parts));
    Add(out, key("totalClusters"), Count(v.testedClusters));
    Add(out, key("visibleClusters"), Count(v.visibleClusters));
    Add(out, key("visibleFrame"), Count(v.visibleFrame));
    Add(out, key("visibleValid"), v.visibleValid);
    Add(out, key("ledgerBytes"), Count(v.ledgerBytes));
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

fx::FeatureDebugResult GpuDrivenSceneDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    const GpuDrivenSceneSettings& s = m_feature.Settings();
    Add(out, "enabled", s.enabled);
    Add(out, "frustum", s.frustum);
    Add(out, "cone", s.cone);
    Add(out, "occlusion", s.occlusion);
    Add(out, "freezeCulling", s.freezeCulling);
    Add(out, "debugView", std::string(GpuDrivenSceneDebugViewName(s.debugView)));
    Add(out, "supported", m_feature.Supported());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult GpuDrivenSceneDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    GpuDrivenSceneSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        switch (ApplyGpuDrivenSceneSetting(candidate, arg.key, arg.value)) {
        case GpuDrivenSceneSetResult::Applied: break;
        case GpuDrivenSceneSetResult::BadValue:
            return Fail(arg.key, arg.key == "debugView" ? "debugView must be Off or ClusterId" : "value must be true or false");
        case GpuDrivenSceneSetResult::UnknownKey: {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.") || arg.key == "supported";
            return Fail(arg.key, readOnly ? "key is read-only" : "unknown key");
        }
        }
    }
    m_feature.ApplySettings(candidate);
    return GetState();
}

fx::FeatureDebugResult GpuDrivenSceneDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
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
    m_feature.ApplySettings(GpuDrivenSceneSettings{});
    return GetState();
}

} // namespace gte
