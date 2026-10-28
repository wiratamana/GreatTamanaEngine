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
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

// State keys that cannot be set.
constexpr std::string_view kReadOnlyKey = "supported";

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
    const auto text = [](std::string_view s) { return std::string(s); };
    const GpuPathReport& g = v.gpu;
    Add(out, key("rendered"), v.rendered);
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("occlusion"), text(fx::EnumName(kOcclusionStateTable, g.occlusion)));
    Add(out, key("hizReason"), text(fx::EnumName(kHizReasonTable, g.hizReason)));
    Add(out, key("inertReason"), text(fx::ClusterInertReasonName(g.inertReason)));
    Add(out, key("lastRefusalReason"), g.refusal);
    Add(out, key("claimedEntities"), Count(g.claimedEntities));
    Add(out, key("parts"), Count(g.parts));
    Add(out, key("testedClusters"), Count(g.testedClusters));
    Add(out, key("testedAtVisibleFrame"), Count(g.testedAtVisibleFrame));
    Add(out, key("visibleClusters"), Count(g.visibleClusters));
    Add(out, key("visibleFrame"), Count(g.visibleFrame));
    Add(out, key("visibleAge"), Count(ClusterCountAgeFrames(v.frameIndex, g.visibleFrame)));
    Add(out, key("hidden"), Count(g.hidden));
    Add(out, key("hiddenValid"), g.hiddenValid);
    Add(out, key("statsCollected"), g.statsCollected);
    Add(out, key("visibleValid"), g.visibleValid);
    Add(out, key("cameraValid"), v.cameraValid);
    Add(out, key("ledgerBytes"), Count(g.ledgerBytes));
    Add(out, key("cpuTested"), Count(v.cpuTested));
    Add(out, key("cpuCulled"), Count(v.cpuCulled));
    Add(out, key("cpuUnbounded"), Count(v.cpuUnbounded));
    Add(out, key("cpuCullState"), text(fx::EnumName(kCpuCullStateTable, v.cpuCullState)));
    Add(out, key("cullCamera"), text(fx::EnumName(kCullCameraSourceTable, v.cullCamera)));
    Add(out, key("hizSource"), text(fx::EnumName(kCullCameraSourceTable, g.hizSource)));
    Add(out, key("sharedSurvivors"), g.sharedSurvivors);
    Add(out, key("cpuPath.skinned"), Count(g.cpuPath.skinned));
    Add(out, key("cpuPath.noIndexBuffer"), Count(g.cpuPath.noIndexBuffer));
    Add(out, key("cpuPath.unsupportedLayout"), Count(g.cpuPath.unsupportedLayout));
    Add(out, key("cpuPath.noClusterTable"), Count(g.cpuPath.noClusterTable));
    Add(out, key("cpuPath.tooFewInstances"), Count(g.cpuPath.tooFewInstances));
    Add(out, key("cpuPath.nonFinite"), Count(g.cpuPath.nonFinite));
    Add(out, key("cpuPath.noSlot"), Count(g.cpuPath.noSlot));
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

bool IsReadOnlyKey(std::string_view key) noexcept
{
    return key.starts_with("game.") || key.starts_with("scene.") || key == kReadOnlyKey;
}

} // namespace

fx::FeatureDebugResult GpuDrivenSceneDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    const GpuDrivenSceneSettings& s = m_feature.Settings();
    Add(out, "enabled", s.enabled);
    Add(out, "mode", std::string(fx::EnumName(kCullModeTable, s.mode)));
    Add(out, "cone", s.cone);
    Add(out, "collectStats", s.collectStats);
    Add(out, "debugView", std::string(fx::EnumName(kGpuDrivenSceneDebugViewTable, s.debugView)));
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
        case GpuDrivenSceneSetResult::BadValue: {
            const GpuDrivenSceneSettingKey* entry = FindGpuDrivenSceneSettingKey(arg.key);
            return Fail(arg.key, arg.key + " must be " + std::string(entry != nullptr ? entry->allowed : "a valid value"));
        }
        case GpuDrivenSceneSetResult::UnknownKey:
            return Fail(arg.key, IsReadOnlyKey(arg.key) ? "key is read-only" : "unknown key");
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
        return Fail(args.front().key, std::string(action) + " takes no arguments");
    }
    m_feature.ApplySettings(GpuDrivenSceneSettings{});
    return GetState();
}

} // namespace gte
