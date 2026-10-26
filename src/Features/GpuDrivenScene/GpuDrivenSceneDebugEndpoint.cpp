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
constexpr std::array<std::string_view, 3> kReadOnlyKeys{ "supported", "sceneCullMode", "freezeSerial" };

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
    Add(out, key("rendered"), v.rendered);
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("frozen"), v.frozen);
    Add(out, key("cullFrozenFrame"), Count(v.frozenFrame));
    Add(out, key("occlusion"), text(fx::EnumName(fx::kOcclusionStateTable, v.occlusion)));
    Add(out, key("hizReason"), text(fx::EnumName(fx::kHizReasonTable, v.hizReason)));
    Add(out, key("inertReason"), text(fx::ClusterInertReasonName(v.inertReason)));
    Add(out, key("lastRefusalReason"), v.refusal);
    Add(out, key("claimedEntities"), Count(v.claimedEntities));
    Add(out, key("parts"), Count(v.parts));
    Add(out, key("testedClusters"), Count(v.testedClusters));
    Add(out, key("testedAtVisibleFrame"), Count(v.testedAtVisibleFrame));
    Add(out, key("visibleClusters"), Count(v.visibleClusters));
    Add(out, key("visibleFrame"), Count(v.visibleFrame));
    Add(out, key("hidden"), Count(v.hidden));
    Add(out, key("hiddenValid"), v.hiddenValid);
    Add(out, key("statsCollected"), v.statsCollected);
    Add(out, key("visibleValid"), v.visibleValid);
    Add(out, key("cameraValid"), v.cameraValid);
    Add(out, key("hizTrackingRequested"), v.hizTrackingRequested);
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

bool IsReadOnlyKey(std::string_view key) noexcept
{
    return key.starts_with("game.") || key.starts_with("scene.")
        || std::find(kReadOnlyKeys.begin(), kReadOnlyKeys.end(), key) != kReadOnlyKeys.end();
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
    Add(out, "collectStats", s.collectStats);
    Add(out, "debugView", std::string(fx::EnumName(kGpuDrivenSceneDebugViewTable, s.debugView)));
    Add(out, "sceneCull", std::string(fx::EnumName(fx::kViewCullChoiceTable, s.sceneCull)));
    Add(out, "sceneCullMode", std::string(fx::EnumName(fx::kViewCullModeTable, CullModeOf(ResolveViewCullTests(s, true)))));
    Add(out, "sceneCone", s.sceneCone);
    Add(out, "sceneSource", std::string(fx::EnumName(fx::kViewCullSourceTable, s.sceneSource)));
    Add(out, "freezeSerial", Count(s.freezeSerial));
    Add(out, "ghost", std::string(fx::EnumName(fx::kGhostStyleTable, s.ghost)));
    Add(out, "supported", m_feature.Supported());
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult GpuDrivenSceneDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    GpuDrivenSceneSettings candidate = m_feature.EditableSettings();
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
    const bool reset = action == "reset_defaults";
    if (!reset && action != "freeze_here") {
        fx::FeatureDebugResult result;
        result.ok = false;
        result.message = "unknown action; supported: reset_defaults, freeze_here";
        return result;
    }
    if (!args.empty()) {
        return Fail(args.front().key, std::string(action) + " takes no arguments");
    }
    GpuDrivenSceneSettings settings = m_feature.EditableSettings();
    if (reset) {
        settings = DefaultsKeepingSerial(settings);
    } else {
        FreezeSceneCullHere(settings);
    }
    m_feature.ApplySettings(settings);
    return GetState();
}

} // namespace gte
