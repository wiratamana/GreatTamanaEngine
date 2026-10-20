#include "FrameDebuggerSideChannelChecker.h"

#include "../Core/ViewBackgroundReplayKey.h"
#include "../Core/FeatureKit/ChannelKey.h"
#include "../Core/FeatureKit/SceneSlotKey.h"

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

// A feature channel published once per frame by the Shared callback.
void AddSharedChannelRule(std::vector<KnownRiskBlackboardKeyRule>& rules, const char* feature, const char* channel,
    const char* gatingToggle)
{
    rules.push_back(KnownRiskBlackboardKeyRule{
        rg::RenderPassId{ fx::ChannelKey(feature, channel, rg::RenderViewId::Shared()) },
        std::string(feature) + "." + channel + " (Shared)", gatingToggle });
}

// A texture published to a scene slot, once per view.
void AddSceneSlotRules(std::vector<KnownRiskBlackboardKeyRule>& rules, const std::string& slotName,
    std::uint32_t slotIndex, const char* gatingToggle)
{
    for (const char* view : { "Game", "Scene" }) {
        rules.push_back(KnownRiskBlackboardKeyRule{ fx::SceneSlotKey(slotIndex, rg::RenderViewId::Named(view)),
            slotName + " (" + view + ")", gatingToggle });
    }
}


} // namespace

const std::vector<KnownRiskBlackboardKeyRule>& KnownRiskBlackboardKeyRules()
{
    static const std::vector<KnownRiskBlackboardKeyRule> kRules = [] {
        std::vector<KnownRiskBlackboardKeyRule> rules = {
            { kViewBackgroundReplayCallbackKey, "Core.ViewBackgroundReplayCallback", "DrawSkyBackground" },
            { "GpuSkinning.OutputBuffers"_passId, "GpuSkinning.OutputBuffers", "GpuSkinning" },
        };
        AddChannelRules(rules, "Shadow", "Map", "Shadow.Cascade0.Draw");
        AddChannelRules(rules, "Shadow", "Receiver", "Shadow.Cascade0.Draw");
        AddChannelRules(rules, "Shadow", "Mask", "Shadow.Mask.Dispatch");
        // Cascade depth maps are published to scene slots 0..3 together with the Map channel.
        for (std::uint32_t slot = 0; slot < 4; ++slot) {
            AddSceneSlotRules(rules, "Shadow.Cascade" + std::to_string(slot) + ".Slot", slot, "Shadow.Cascade0.Draw");
        }
        // Published only when both passes ran, so either toggle gates it.
        AddChannelRules(rules, "VolumetricLight", "Result", "VolumetricLight.Inject.Dispatch");
        AddChannelRules(rules, "VolumetricLight", "Result", "VolumetricLight.Integrate.Dispatch");
        // Published only when both LUT passes ran, so either toggle gates it.
        AddSharedChannelRule(rules, "Atmosphere", "SharedLuts", "AtmosphereTransmittanceLutPass");
        AddSharedChannelRule(rules, "Atmosphere", "SharedLuts", "AtmosphereMultiScatteringLutPass");
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
