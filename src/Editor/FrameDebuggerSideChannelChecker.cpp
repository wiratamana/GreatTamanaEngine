#include "FrameDebuggerSideChannelChecker.h"

namespace gte {

using rg::operator""_passId;

const std::vector<KnownRiskBlackboardKeyRule>& KnownRiskBlackboardKeyRules()
{
    // Every literal string below is a byte-for-byte copy of the matching
    // Core.cpp constant (see this file's own header comment for why a copy,
    // not a shared constant, is the deliberate, precedented choice here).
    static const std::vector<KnownRiskBlackboardKeyRule> kRules = {
        // Core.cpp's kGameSkyBackgroundCallbackKey - the CONFIRMED,
        // reported Root Cause #3 bug this campaign's own PHASE1 fixed
        // (PHASE1_FIX_DRAWSKYBACKGROUND_TOGGLE_SIDE_CHANNEL_LEAK.md): the
        // "DrawSkyBackground" provider used to Publish() this callback
        // unconditionally, letting "FrameDebuggerReplayStepN" redraw the
        // sky even while "DrawSkyBackground" itself was disabled.
        { "Atmosphere.GameSkyBackgroundCallback"_passId, "Atmosphere.GameSkyBackgroundCallback", "DrawSkyBackground" },

        // Core.cpp's own kGameSkyBackgroundReplayCallbackKey - the generic,
        // feature-free twin of the entry above, gated by the same
        // "DrawSkyBackground" toggle and published from the same early-
        // guarded call site.
        { "Core.GameSkyBackgroundReplayCallback"_passId, "Core.GameSkyBackgroundReplayCallback", "DrawSkyBackground" },

        // Core.cpp's kGpuSkinningOutputsKey - PHASE2's own ledger finding
        // #6/#9: already gated by "GpuSkinning"'s own early guard (the
        // pattern PHASE1's fix generalized FROM), but the exact same risk
        // SHAPE - kept here as a permanent regression tripwire, not because
        // it is currently broken.
        { "GpuSkinning.OutputBuffers"_passId, "GpuSkinning.OutputBuffers", "GpuSkinning" },

        // Core::ViewCompositedOutputKey()'s two reserved slots - PHASE2's
        // own ledger finding #10: already gated by an explicit
        // `if (!composited.IsValid()) return;` immediately before Publish(),
        // itself downstream of the deferred composite pass's own self-gated
        // NoteDeclaredAndCheckEnabled() check (finding #11) - same reasoning
        // as kGpuSkinningOutputsKey above, kept as a tripwire.
        { "Core.ViewCompositedOutput.Game"_passId, "Core.ViewCompositedOutput.Game", "AtmosphereComposite" },
        { "Core.ViewCompositedOutput.Scene"_passId, "Core.ViewCompositedOutput.Scene", "AtmosphereComposite" },
    };
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
            // The gating toggle is enabled - publishing this key this frame
            // is expected/honest, regardless of whether it actually did.
            continue;
        }
        if (wasPublishedLookup(rule.key)) {
            // The gating toggle reports DISABLED, yet this key was
            // published anyway this frame - a genuine side-channel leak.
            leaks.push_back(rule.keyDebugName);
        }
    }
    return leaks;
}

} // namespace gte
