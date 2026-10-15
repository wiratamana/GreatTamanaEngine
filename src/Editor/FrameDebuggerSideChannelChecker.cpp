#include "FrameDebuggerSideChannelChecker.h"

#include "../Core/FeatureKit/ChannelKey.h"

namespace gte {

using rg::operator""_passId;

namespace {

// A feature channel, published once per view under the key the kit builds for it.
void AddChannelRules(std::vector<KnownRiskBlackboardKeyRule>& rules, const char* feature, const char* channel,
    const char* gatingToggle)
{
    for (const char* view : { "Game", "Scene" }) {
        rules.push_back(KnownRiskBlackboardKeyRule{
            rg::RenderPassId{ fx::ChannelKey(feature, channel, rg::RenderViewId::Named(view)) },
            std::string(feature) + "." + channel + " (" + view + ")", gatingToggle });
    }
}

} // namespace

const std::vector<KnownRiskBlackboardKeyRule>& KnownRiskBlackboardKeyRules()
{
    // The literals mirror the keys the engine publishes; keep them in sync.
    static const std::vector<KnownRiskBlackboardKeyRule> kRules = [] {
        std::vector<KnownRiskBlackboardKeyRule> rules = {
            { "Atmosphere.GameSkyBackgroundCallback"_passId, "Atmosphere.GameSkyBackgroundCallback", "DrawSkyBackground" },
            { "Core.GameSkyBackgroundReplayCallback"_passId, "Core.GameSkyBackgroundReplayCallback", "DrawSkyBackground" },
            { "GpuSkinning.OutputBuffers"_passId, "GpuSkinning.OutputBuffers", "GpuSkinning" },
        };
        AddChannelRules(rules, "Shadow", "Map", "Shadow.DepthPass.Draw");
        AddChannelRules(rules, "Shadow", "Mask", "Shadow.Mask.Dispatch");
        return rules;
    }();
    return kRules;
}

std::vector<std::string> DetectDisabledPassBlackboardKeyLeaks(const std::vector<KnownRiskBlackboardKeyRule>& rules,
    const std::function<bool(const std::string&)>& isEnabledLookup,
    const std::function<bool(rg::RenderPassId)>& wasPublishedLookup)
{
    std::vector<std::string> leaks;
    if (!isEnabledLookup || !wasPublishedLookup) {
        return leaks;
    }
    for (const KnownRiskBlackboardKeyRule& rule : rules) {
        if (isEnabledLookup(rule.gatingToggleName)) {
            continue; // Publishing while enabled is expected.
        }
        if (wasPublishedLookup(rule.key)) {
            leaks.push_back(rule.keyDebugName);
        }
    }
    return leaks;
}

} // namespace gte
